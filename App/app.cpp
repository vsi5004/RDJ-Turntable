/**
 * app.cpp - application composition root and cooperative main-loop tick.
 *
 * Until the full mechanics are assembled the firmware boots into the diagnostic authority. The
 * proven M2c platter exercises are now typed diagnostic commands rather than independent booleans
 * and duplicate key polling. Normal turntable behavior is present behind unavailable-safe adapters.
 */
#include "app.h"

#include "board.h"
#include "control/as5048a.h"
#include "control/foc.h"
#include "control/mt6826s.h"
#include "diagnostics/diagnostic_controller.hpp"
#include "diagnostics/m2c_executor.hpp"
#include "hmi/screens.h"
#include "nvm.h"
#include "platform/debounced_button.hpp"
#include "platform/system_adapters.hpp"
#include "trace.h"
#include "turntable/application_controller.hpp"

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kOpenLoopElecVel = 2.0f * kPi;
constexpr float kBeltRatio = 90.0f / 24.0f;
constexpr float kMotorVelocity33 = 33.3333f * kBeltRatio * (2.0f * kPi / 60.0f);
constexpr uint32_t kGlobalStopHoldMs = 800;
constexpr uint32_t kCalibrationHoldMs = 2500;

platform::HalClock clock_source;
platform::BenchPlatter platter;
platform::UnavailableCarriage carriage;
platform::UnavailableLift lift;
turntable::Controller turntable_controller(clock_source, platter, carriage, lift);
diagnostics::M2cExecutor m2c_executor(clock_source);
diagnostics::Controller diagnostic_controller(m2c_executor);
turntable::ApplicationController application(turntable_controller, diagnostic_controller);

gpio::DebouncedButton key0{gpio::InputPin{KEY0_GPIO_Port, KEY0_Pin, false}};
gpio::DebouncedButton key1{gpio::InputPin{KEY1_GPIO_Port, KEY1_Pin, false}};
gpio::DebouncedButton key2{gpio::InputPin{KEY2_GPIO_Port, KEY2_Pin, false}};

diagnostics::Command command(diagnostics::Action action, float value = 0.0f)
{
    diagnostics::Command out;
    out.target = diagnostics::Target::PlatterMotor;
    out.action = action;
    out.parameters.value = value;
    return out;
}

bool running(diagnostics::Action action)
{
    const diagnostics::Snapshot snapshot = diagnostic_controller.snapshot();
    return snapshot.state == diagnostics::State::Running && snapshot.command.action == action;
}

void handle_keys(uint32_t now)
{
    const gpio::ButtonEvent play = key0.update(now, kGlobalStopHoldMs);
    const gpio::ButtonEvent align = key1.update(now, kCalibrationHoldMs);
    const gpio::ButtonEvent loop = key2.update(now, kGlobalStopHoldMs);

    if (application.authority() == turntable::ControlAuthority::Normal) {
        const turntable::Snapshot snapshot = turntable_controller.snapshot();
        if (play == gpio::ButtonEvent::Held
            && snapshot.actions.contains(turntable::Action::Stop)) {
            application.post(turntable::Event::simple(turntable::EventType::StopRequested));
        } else if (play == gpio::ButtonEvent::Tap) {
            if (snapshot.actions.contains(turntable::Action::Initialize))
                application.post(
                    turntable::Event::simple(turntable::EventType::InitializeRequested));
            else if (snapshot.actions.contains(turntable::Action::Play))
                application.post(turntable::Event::simple(turntable::EventType::PlayRequested));
            else if (snapshot.actions.contains(turntable::Action::Pause))
                application.post(turntable::Event::simple(turntable::EventType::PauseRequested));
            else if (snapshot.actions.contains(turntable::Action::Resume))
                application.post(turntable::Event::simple(turntable::EventType::ResumeRequested));
            else if (snapshot.actions.contains(turntable::Action::AcknowledgeFault))
                application.post(turntable::Event::simple(
                    turntable::EventType::AcknowledgeFaultRequested));
        }

        if (align == gpio::ButtonEvent::Tap
            && snapshot.actions.contains(turntable::Action::SelectSpeed)) {
            const turntable::RecordSpeed next =
                snapshot.selected_speed == turntable::RecordSpeed::Rpm33
                ? turntable::RecordSpeed::Rpm45 : turntable::RecordSpeed::Rpm33;
            application.post(turntable::Event::speed_change(next));
        }
        if (loop == gpio::ButtonEvent::Tap) TRACE("Settings UI not implemented yet.\n");
        return;
    }

    if (diagnostic_controller.state() == diagnostics::State::Fault) {
        if (play == gpio::ButtonEvent::Tap || play == gpio::ButtonEvent::Held) {
            application.acknowledge_diagnostic_fault();
            TRACE("\n>>> DIAGNOSTIC FAULT ACKNOWLEDGED\n");
        }
        return;
    }

    if (play == gpio::ButtonEvent::Held) {
        application.abort_diagnostic();
        TRACE("\n>>> DIAGNOSTIC GLOBAL STOP\n");
    } else if (play == gpio::ButtonEvent::Tap) {
        if (running(diagnostics::Action::OpenLoopSpin)) {
            application.abort_diagnostic();
        } else {
            application.submit_diagnostic(
                command(diagnostics::Action::OpenLoopSpin, kOpenLoopElecVel));
        }
    }

    if (align == gpio::ButtonEvent::Held) {
        application.submit_diagnostic(command(diagnostics::Action::EncoderAutoCal));
    } else if (align == gpio::ButtonEvent::Tap) {
        application.submit_diagnostic(command(diagnostics::Action::ElectricalAlign));
    }

    if (loop == gpio::ButtonEvent::Tap) {
        if (running(diagnostics::Action::ClosedLoopVelocity)) {
            application.abort_diagnostic();
        } else {
            application.submit_diagnostic(
                command(diagnostics::Action::ClosedLoopVelocity, kMotorVelocity33));
        }
    }
}

void trace_status(uint32_t now)
{
    static uint32_t last_trace = 0;
    if (static_cast<uint32_t>(now - last_trace) < 200) return;
    last_trace = now;

    const diagnostics::Snapshot diagnostic = diagnostic_controller.snapshot();
    if (diagnostic.state == diagnostics::State::Running
        && diagnostic.command.action == diagnostics::Action::ClosedLoopVelocity) {
        const int32_t vel_x10 = static_cast<int32_t>(diagnostic.report.primary * 10.0f);
        const int32_t uq_mv = static_cast<int32_t>(diagnostic.report.secondary * 1000.0f);
        TRACE("DIAG LOOP vel=%ld.%ld rad/s Uq=%ldmV FAULT=%u\n",
              static_cast<long>(vel_x10 / 10),
              static_cast<long>(vel_x10 < 0 ? -vel_x10 % 10 : vel_x10 % 10),
              static_cast<long>(uq_mv), static_cast<unsigned>(foc::faulted()));
        return;
    }

    if (diagnostic.state == diagnostics::State::Running
        && diagnostic.command.action == diagnostics::Action::OpenLoopSpin) {
        mt6826s::Reading reading;
        mt6826s::read(reading);
        const uint32_t mech_x10 = static_cast<uint32_t>(reading.angle) * 3600u / 32768u;
        TRACE("DIAG SPIN mech=%u.%u deg crc=%s FAULT=%u\n",
              static_cast<unsigned>(mech_x10 / 10), static_cast<unsigned>(mech_x10 % 10),
              reading.crc_ok ? "OK" : "BAD", static_cast<unsigned>(foc::faulted()));
        return;
    }

    if (diagnostic.state == diagnostics::State::Ready) {
        mt6826s::Reading platter_angle;
        mt6826s::read(platter_angle);
        as5048a::Reading arm_angle;
        as5048a::read(arm_angle);
        TRACE("DIAG READY platter=%u crc=%s arm=%u parity=%s\n",
              platter_angle.angle, platter_angle.crc_ok ? "OK" : "BAD", arm_angle.angle,
              arm_angle.parity_ok ? "OK" : "BAD");
    }
}

}  // namespace

void app_init(void)
{
    trace_init();
    led.off();
    TRACE("\n=== RDJ-Turntable diagnostic architecture ===\n");
    TRACE("SYSCLK = %u Hz\n", static_cast<unsigned>(HAL_RCC_GetSysClockFreq()));

    screens::init();
    foc::init();

    nvm::Cal cal;
    if (nvm::load(cal)) {
        foc::zero_elec_offset = cal.zero_offset;
        foc::direction = static_cast<int8_t>(cal.direction);
        foc::alignment_valid = true;
        TRACE("Loaded platter alignment: dir=%c\n", cal.direction > 0 ? '+' : '-');
    } else {
        TRACE("No stored platter alignment; run KEY1 tap before closed-loop velocity.\n");
    }

#if RDJ_BOOT_DIAGNOSTICS
    application.boot_diagnostics();
#endif
    screens::show(hmi::present(application.snapshot()));
#if RDJ_BOOT_DIAGNOSTICS
    TRACE("Diagnostics ready: KEY0 spin, KEY1 align / hold auto-cal, KEY2 velocity.\n");
    TRACE("Hold KEY0 for global stop.\n");
#else
    TRACE("Normal control authority ready.\n");
#endif
}

void app_run(void)
{
    static uint32_t last_blink = 0;
    const uint32_t now = HAL_GetTick();

    handle_keys(now);
    application.tick();
    screens::show(hmi::present(application.snapshot()));

    if (static_cast<uint32_t>(now - last_blink) >= 250) {
        last_blink = now;
        led.toggle();
    }

    trace_status(now);
    screens::tick();
}
