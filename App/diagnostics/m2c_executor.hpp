#pragma once

#include "diagnostic_controller.hpp"
#include "turntable/interfaces.hpp"

namespace diagnostics {

class M2cExecutor final : public IExecutor {
public:
    explicit M2cExecutor(turntable::IClock& clock) : clock_(clock) {}

    bool start(const Command& command) override;
    void abort() override;
    void safe_stop() override;
    ExecutionReport poll() override;

private:
    enum class Phase : uint8_t {
        Idle,
        Continuous,
        ReadStatus,
        AlignWaitZero,
        AlignWaitStep,
        CalRamp,
        CalSettle,
        CalMeasure,
        CalPoll,
        JogSettle,
    };

    bool read_mechanical_rad(float& angle);
    void finish(ExecutionState state, int32_t result_code = 0);
    void restore_calibration_voltage();
    bool elapsed(uint32_t duration_ms) const;

    /* Per-motor dispatch by command_.target: platter (foc/MT6826S) vs tonearm (arm_foc/AS5048A).
     * Lets the shared electrical-align state machine run for both axes. */
    bool is_tonearm() const;
    void motor_hold_electrical_angle(float theta_el);
    bool motor_faulted() const;
    bool commit_alignment(float offset, int dir);
    void stop_motors();

    turntable::IClock& clock_;
    Command command_{};
    ExecutionReport report_{};
    Phase phase_ = Phase::Idle;
    uint32_t phase_started_ms_ = 0;
    uint32_t cal_poll_count_ = 0;
    int cal_ramp_step_ = 0;
    float align_m0_ = 0.0f;
    float cal_angle0_ = 0.0f;
    float saved_voltage_limit_ = 0.0f;
    bool cal_voltage_saved_ = false;
};

}  // namespace diagnostics
