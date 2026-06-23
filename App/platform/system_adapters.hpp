#pragma once

#include "turntable/interfaces.hpp"

namespace platform {

class HalClock final : public turntable::IClock {
public:
    uint32_t now_ms() const override;
};

class BenchPlatter final : public turntable::IPlatter {
public:
    void start(turntable::RecordSpeed speed, uint32_t operation_id) override;
    void stop(uint32_t operation_id) override;
    void emergency_stop() override;
};

class UnavailableCarriage final : public turntable::ITonearmCarriage {
public:
    void home(uint32_t operation_id) override;
    void establish_home_reference() override;
    void move_to_park(uint32_t operation_id) override;
    void move_to_lead_in(uint32_t operation_id) override;
    void stop() override;
    void set_tracking(bool enabled) override;
    void mark_home_valid() override;
    void invalidate_home() override;
    turntable::HomeConfidence home_confidence() const override;
    bool at_park() const override;
};

class UnavailableLift final : public turntable::ITonearmLift {
public:
    void raise(uint32_t operation_id) override;
    void lower(uint32_t operation_id) override;
};

}  // namespace platform
