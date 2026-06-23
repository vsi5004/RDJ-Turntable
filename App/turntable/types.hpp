#pragma once

#include <cstdint>

namespace turntable {

enum class RecordSpeed : uint8_t { Rpm33, Rpm45 };
enum class HomeConfidence : uint8_t { Unknown, Valid };
enum class PositionConfidence : uint8_t { Unknown, Estimated, Verified };
enum class LiftPosition : uint8_t { Unknown, Raised, Lowered };
enum class MotionState : uint8_t { Idle, Moving, AtTarget, Fault };

enum class State : uint8_t {
    NeedsHome,
    RaisingForInitialization,
    HomingCarriage,
    ParkingCarriage,
    Idle,
    SpinningUpForPlay,
    SeekingLeadIn,
    LoweringForPlay,
    Playing,
    RaisingForPause,
    Paused,
    LoweringForResume,
    RaisingForSpeedChange,
    ChangingSpeedForPlayback,
    LoweringAfterSpeedChange,
    ChangingSpeedWhilePaused,
    RaisingForStop,
    ReturningHomeWithPlatter,
    StoppingPlatter,
    Interrupted,
    SpinningUpForResume,
    ReturningHomeWithoutPlatter,
    Maintenance,
    Fault,
};

enum class FaultSource : uint8_t { Product, Platter, Carriage, Lift, Diagnostic };
enum class RecoveryPolicy : uint8_t { Retryable, RequiresCarriageHome, RequiresPowerCycle };

enum class FaultCode : uint8_t {
    None,
    PlatterDriver,
    PlatterEncoder,
    PlatterLockTimeout,
    CarriageHomeTimeout,
    CarriageSeekTimeout,
    CarriageReturnTimeout,
    CarriageEncoder,
    CarriageStall,
    CarriageSoftwareLimit,
    MaintenanceTimeout,
    MaintenanceFailed,
    DiagnosticFailure,
    Unknown,
};

enum class MaintenanceOperation : uint8_t {
    PlatterAlignment,
    EncoderCalibration,
    CarriageRehome,
};

struct FaultRecord {
    FaultCode code = FaultCode::None;
    FaultSource source = FaultSource::Product;
    RecoveryPolicy recovery = RecoveryPolicy::Retryable;
    bool invalidates_home = false;
    uint32_t occurred_at_ms = 0;
};

enum class EventType : uint8_t {
    None,
    InitializeRequested,
    CancelRequested,
    PlayRequested,
    PauseRequested,
    ResumeRequested,
    StopRequested,
    SpeedChangeRequested,
    MaintenanceRequested,
    MaintenanceCompleted,
    MaintenanceCancelRequested,
    AcknowledgeFaultRequested,
    FaultConditionCleared,
    LiftSettled,
    HomeReferenceFound,
    CarriageAtPark,
    CarriageAtLeadIn,
    PlatterSpeedLocked,
    PlatterStopped,
    PlatterSpeedLost,
    EndOfSideDetected,
    DeadlineExpired,
    FaultDetected,
};

struct Event {
    EventType type = EventType::None;
    uint32_t operation_id = 0;
    RecordSpeed speed = RecordSpeed::Rpm33;
    LiftPosition lift_position = LiftPosition::Unknown;
    PositionConfidence confidence = PositionConfidence::Unknown;
    MaintenanceOperation maintenance = MaintenanceOperation::PlatterAlignment;
    FaultRecord fault{};

    static constexpr Event simple(EventType type) { return Event{type}; }

    static constexpr Event speed_change(RecordSpeed speed)
    {
        Event event{EventType::SpeedChangeRequested};
        event.speed = speed;
        return event;
    }

    static constexpr Event maintenance_request(MaintenanceOperation operation)
    {
        Event event{EventType::MaintenanceRequested};
        event.maintenance = operation;
        return event;
    }

    static constexpr Event completion(EventType type, uint32_t operation_id)
    {
        Event event{type};
        event.operation_id = operation_id;
        return event;
    }

    static constexpr Event lift_settled(uint32_t operation_id, LiftPosition position,
                                        PositionConfidence confidence)
    {
        Event event{EventType::LiftSettled};
        event.operation_id = operation_id;
        event.lift_position = position;
        event.confidence = confidence;
        return event;
    }

    static constexpr Event detected_fault(FaultRecord fault)
    {
        Event event{EventType::FaultDetected};
        event.fault = fault;
        return event;
    }
};

enum class Action : uint32_t {
    None = 0,
    Initialize = 1u << 0,
    Cancel = 1u << 1,
    Play = 1u << 2,
    Pause = 1u << 3,
    Resume = 1u << 4,
    Stop = 1u << 5,
    SelectSpeed = 1u << 6,
    OpenSettings = 1u << 7,
    EnterDiagnostics = 1u << 8,
    AcknowledgeFault = 1u << 9,
    FaultDetails = 1u << 10,
};

class ActionSet {
public:
    constexpr void add(Action action) { bits_ |= static_cast<uint32_t>(action); }
    constexpr bool contains(Action action) const
    {
        return (bits_ & static_cast<uint32_t>(action)) != 0;
    }
    constexpr uint32_t bits() const { return bits_; }

private:
    uint32_t bits_ = 0;
};

struct LiftStatus {
    LiftPosition position = LiftPosition::Unknown;
    MotionState motion = MotionState::Idle;
    PositionConfidence confidence = PositionConfidence::Unknown;
};

struct Observations {
    float measured_rpm = 0.0f;
    bool speed_locked = false;
    float carriage_position_mm = 0.0f;
    LiftStatus lift{};
};

struct Snapshot {
    State state = State::NeedsHome;
    RecordSpeed selected_speed = RecordSpeed::Rpm33;
    float measured_rpm = 0.0f;
    bool speed_locked = false;
    HomeConfidence home = HomeConfidence::Unknown;
    float carriage_position_mm = 0.0f;
    LiftStatus lift{};
    FaultRecord fault{};
    ActionSet actions{};
};

struct ControllerConfig {
    uint32_t lift_raise_deadline_ms = 1500;
    uint32_t lift_lower_deadline_ms = 1500;
    uint32_t carriage_home_deadline_ms = 15000;
    uint32_t carriage_move_deadline_ms = 10000;
    uint32_t platter_lock_deadline_ms = 15000;
    uint32_t platter_stop_deadline_ms = 10000;
    uint32_t maintenance_deadline_ms = 30000;
};

}  // namespace turntable
