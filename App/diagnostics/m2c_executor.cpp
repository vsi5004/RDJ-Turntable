#include "m2c_executor.hpp"

#include "control/foc.h"
#include "control/mt6826s.h"
#include "nvm.h"
#include "trace.h"

#include <cmath>

namespace diagnostics {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kOpenLoopElecVel = kTwoPi;
constexpr float kBeltRatio = 90.0f / 24.0f;
constexpr float kMotorVelocity33 = 33.3333f * kBeltRatio * (kTwoPi / 60.0f);
constexpr float kCalRpm = 180.0f;
constexpr uint8_t kCalFreq = 0x5;
constexpr float kCalVoltage = 3.0f;

float shortest_delta(float a, float b)
{
    float d = a - b;
    if (d > kPi) d -= kTwoPi;
    if (d < -kPi) d += kTwoPi;
    return d;
}

}  // namespace

bool M2cExecutor::start(const Command& command)
{
    if (phase_ != Phase::Idle) return false;
    command_ = command;
    report_ = {ExecutionState::Running};
    phase_started_ms_ = clock_.now_ms();

    switch (command.action) {
    case Action::ReadStatus:
        phase_ = Phase::ReadStatus;
        return true;
    case Action::OpenLoopSpin:
        foc::set_open_loop_electrical_velocity(
            command.parameters.value != 0.0f ? command.parameters.value : kOpenLoopElecVel);
        phase_ = Phase::Continuous;
        TRACE("diag: platter open-loop started\n");
        return true;
    case Action::ElectricalAlign:
        foc::hold_electrical_angle(0.0f);
        phase_ = Phase::AlignWaitZero;
        TRACE("diag: electrical alignment started\n");
        return true;
    case Action::EncoderAutoCal:
        mt6826s::set_autocal_freq(kCalFreq);
        saved_voltage_limit_ = foc::voltage_limit;
        cal_voltage_saved_ = true;
        foc::voltage_limit = kCalVoltage;
        cal_ramp_step_ = 1;
        foc::set_open_loop_electrical_velocity(
            (kCalRpm / 60.0f) * kTwoPi * foc::pole_pairs / 40.0f);
        phase_ = Phase::CalRamp;
        TRACE("diag: encoder auto-cal ramp started\n");
        return true;
    case Action::ClosedLoopVelocity:
        if (!foc::alignment_valid) {
            report_ = {ExecutionState::Failed, -8};
            TRACE("diag: velocity rejected; platter alignment is not valid\n");
            return false;
        }
        foc::set_closed_loop_velocity(
            command.parameters.value != 0.0f ? command.parameters.value : kMotorVelocity33);
        phase_ = Phase::Continuous;
        TRACE("diag: platter velocity loop started\n");
        return true;
    case Action::Stop:
        safe_stop();
        report_ = {ExecutionState::Complete};
        return true;
    default:
        report_ = {ExecutionState::Failed, -1};
        return false;
    }
}

void M2cExecutor::abort()
{
    if (phase_ == Phase::CalPoll) mt6826s::stop_autocal();
    foc::stop();
    restore_calibration_voltage();
    phase_ = Phase::Idle;
    report_ = {ExecutionState::Idle};
    TRACE("diag: operation aborted; platter disabled\n");
}

void M2cExecutor::safe_stop()
{
    if (phase_ == Phase::CalPoll) mt6826s::stop_autocal();
    foc::stop();
    restore_calibration_voltage();
    phase_ = Phase::Idle;
    report_ = {ExecutionState::Idle};
}

ExecutionReport M2cExecutor::poll()
{
    if (phase_ == Phase::Idle) return report_;

    if (foc::faulted()) {
        finish(ExecutionState::Failed, -2);
        return report_;
    }

    switch (phase_) {
    case Phase::Continuous:
        if (command_.action == Action::ClosedLoopVelocity) {
            report_.primary = foc::velocity();
            report_.secondary = foc::applied_uq();
        } else {
            report_.primary = foc::electrical_angle();
        }
        break;

    case Phase::ReadStatus: {
        mt6826s::Reading reading;
        mt6826s::read(reading);
        report_.primary = mt6826s::degrees(reading.angle);
        report_.secondary = static_cast<float>(reading.status);
        finish(reading.crc_ok ? ExecutionState::Complete : ExecutionState::Failed,
               reading.crc_ok ? 0 : -3);
        break;
    }

    case Phase::AlignWaitZero:
        if (elapsed(900)) {
            if (!read_mechanical_rad(align_m0_)) {
                finish(ExecutionState::Failed, -3);
                break;
            }
            foc::hold_electrical_angle(kPi / 3.0f);
            phase_started_ms_ = clock_.now_ms();
            phase_ = Phase::AlignWaitStep;
        }
        break;

    case Phase::AlignWaitStep:
        if (elapsed(900)) {
            float m1 = 0.0f;
            if (!read_mechanical_rad(m1)) {
                finish(ExecutionState::Failed, -3);
                break;
            }
            const float dmech = shortest_delta(m1, align_m0_);
            foc::stop();
            foc::direction = dmech >= 0.0f ? 1 : -1;
            foc::zero_elec_offset = align_m0_;
            foc::alignment_valid = true;
            nvm::Cal cal{foc::zero_elec_offset, foc::direction};
            const bool saved = nvm::save(cal);
            report_.primary = align_m0_ * (180.0f / kPi);
            report_.secondary = dmech * (180.0f / kPi);
            TRACE("diag: align offset=%u deg dir=%c save=%s\n",
                  static_cast<unsigned>(report_.primary), foc::direction > 0 ? '+' : '-',
                  saved ? "OK" : "FAILED");
            finish(saved ? ExecutionState::Complete : ExecutionState::Failed, saved ? 0 : -4);
        }
        break;

    case Phase::CalRamp:
        if (elapsed(50)) {
            ++cal_ramp_step_;
            if (cal_ramp_step_ > 40) {
                phase_ = Phase::CalSettle;
            } else {
                const float target = (kCalRpm / 60.0f) * kTwoPi * foc::pole_pairs;
                foc::set_open_loop_electrical_velocity(target * cal_ramp_step_ / 40.0f);
            }
            phase_started_ms_ = clock_.now_ms();
        }
        break;

    case Phase::CalSettle:
        if (elapsed(1000)) {
            if (!read_mechanical_rad(cal_angle0_)) {
                finish(ExecutionState::Failed, -3);
                break;
            }
            phase_started_ms_ = clock_.now_ms();
            phase_ = Phase::CalMeasure;
        }
        break;

    case Phase::CalMeasure:
        if (elapsed(100)) {
            float angle1 = 0.0f;
            if (!read_mechanical_rad(angle1)) {
                finish(ExecutionState::Failed, -3);
                break;
            }
            const float delta = shortest_delta(angle1, cal_angle0_);
            const float rpm = std::fabs((delta / 0.1f) * (60.0f / kTwoPi));
            report_.primary = rpm;
            if (rpm < 80.0f || rpm > 200.0f) {
                TRACE("diag: auto-cal speed out of band (%ld RPM)\n",
                      static_cast<long>(rpm));
                finish(ExecutionState::Failed, -5);
            } else {
                mt6826s::start_autocal();
                cal_poll_count_ = 0;
                phase_started_ms_ = clock_.now_ms();
                phase_ = Phase::CalPoll;
                TRACE("diag: encoder auto-cal running\n");
            }
        }
        break;

    case Phase::CalPoll:
        if (elapsed(100)) {
            phase_started_ms_ = clock_.now_ms();
            ++cal_poll_count_;
            const mt6826s::CalStatus status = mt6826s::autocal_status();
            report_.secondary = static_cast<float>(status);
            if (status == mt6826s::CalStatus::Success) {
                TRACE("diag: encoder auto-cal success; power-cycle required\n");
                finish(ExecutionState::Complete);
            } else if (status == mt6826s::CalStatus::Failed || cal_poll_count_ >= 200) {
                mt6826s::stop_autocal();
                finish(ExecutionState::Failed,
                       status == mt6826s::CalStatus::Failed ? -6 : -7);
            }
        }
        break;

    case Phase::Idle:
        break;
    }

    return report_;
}

bool M2cExecutor::read_mechanical_rad(float& angle)
{
    mt6826s::Reading reading;
    mt6826s::read(reading);
    if (!reading.crc_ok) return false;
    angle = static_cast<float>(reading.angle) * (kTwoPi / 32768.0f);
    return true;
}

void M2cExecutor::finish(ExecutionState state, int32_t result_code)
{
    if (phase_ == Phase::CalPoll && state == ExecutionState::Failed)
        mt6826s::stop_autocal();
    foc::stop();
    restore_calibration_voltage();
    phase_ = Phase::Idle;
    report_.state = state;
    report_.result_code = result_code;
}

void M2cExecutor::restore_calibration_voltage()
{
    if (!cal_voltage_saved_) return;
    foc::voltage_limit = saved_voltage_limit_;
    cal_voltage_saved_ = false;
}

bool M2cExecutor::elapsed(uint32_t duration_ms) const
{
    return static_cast<uint32_t>(clock_.now_ms() - phase_started_ms_) >= duration_ms;
}

}  // namespace diagnostics
