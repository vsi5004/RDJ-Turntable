#pragma once

#include "types.hpp"

namespace turntable {

class IClock {
public:
    virtual ~IClock() = default;
    virtual uint32_t now_ms() const = 0;
};

class IPlatter {
public:
    virtual ~IPlatter() = default;
    virtual void start(RecordSpeed speed, uint32_t operation_id) = 0;
    virtual void stop(uint32_t operation_id) = 0;
    virtual void emergency_stop() = 0;
};

class ITonearmCarriage {
public:
    virtual ~ITonearmCarriage() = default;
    virtual void home(uint32_t operation_id) = 0;
    virtual void establish_home_reference() = 0;
    virtual void move_to_park(uint32_t operation_id) = 0;
    virtual void move_to_lead_in(uint32_t operation_id) = 0;
    virtual void stop() = 0;
    virtual void set_tracking(bool enabled) = 0;
    virtual void mark_home_valid() = 0;
    virtual void invalidate_home() = 0;
    virtual HomeConfidence home_confidence() const = 0;
    virtual bool at_park() const = 0;
};

class ITonearmLift {
public:
    virtual ~ITonearmLift() = default;
    virtual void raise(uint32_t operation_id) = 0;
    virtual void lower(uint32_t operation_id) = 0;
};

class IMaintenanceExecutor {
public:
    virtual ~IMaintenanceExecutor() = default;
    virtual bool start(MaintenanceOperation operation, uint32_t operation_id) = 0;
    virtual void cancel() = 0;
    virtual bool cancellable() const = 0;
};

}  // namespace turntable
