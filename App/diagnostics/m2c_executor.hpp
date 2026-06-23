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
    };

    bool read_mechanical_rad(float& angle);
    void finish(ExecutionState state, int32_t result_code = 0);
    void restore_calibration_voltage();
    bool elapsed(uint32_t duration_ms) const;

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
