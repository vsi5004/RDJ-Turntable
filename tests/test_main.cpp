#include "turntable/application_controller.hpp"
#include "control/platter_feedback.hpp"

#include <cstdio>

namespace {

int failures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                       \
            ++failures;                                                                             \
        }                                                                                           \
    } while (false)

class FakeClock final : public turntable::IClock {
public:
    uint32_t now_ms() const override { return now; }
    uint32_t now = 0;
};

class FakePlatter final : public turntable::IPlatter {
public:
    void start(turntable::RecordSpeed requested, uint32_t operation_id) override
    {
        speed = requested;
        last_operation = operation_id;
        ++starts;
    }
    void stop(uint32_t operation_id) override
    {
        last_operation = operation_id;
        ++stops;
    }
    void emergency_stop() override { ++emergency_stops; }

    turntable::RecordSpeed speed = turntable::RecordSpeed::Rpm33;
    uint32_t last_operation = 0;
    int starts = 0;
    int stops = 0;
    int emergency_stops = 0;
};

class FakeCarriage final : public turntable::ITonearmCarriage {
public:
    void home(uint32_t operation_id) override { last_operation = operation_id; ++homes; }
    void establish_home_reference() override { ++references; }
    void move_to_park(uint32_t operation_id) override
    {
        last_operation = operation_id;
        at_park_value = false;
        ++parks;
    }
    void move_to_lead_in(uint32_t operation_id) override
    {
        last_operation = operation_id;
        at_park_value = false;
        ++seeks;
    }
    void stop() override { ++stops; }
    void set_tracking(bool enabled) override { tracking = enabled; }
    void mark_home_valid() override
    {
        home_value = turntable::HomeConfidence::Valid;
        at_park_value = true;
    }
    void invalidate_home() override { home_value = turntable::HomeConfidence::Unknown; }
    turntable::HomeConfidence home_confidence() const override { return home_value; }
    bool at_park() const override { return at_park_value; }

    turntable::HomeConfidence home_value = turntable::HomeConfidence::Unknown;
    bool at_park_value = false;
    bool tracking = false;
    uint32_t last_operation = 0;
    int homes = 0;
    int references = 0;
    int parks = 0;
    int seeks = 0;
    int stops = 0;
};

class FakeLift final : public turntable::ITonearmLift {
public:
    void raise(uint32_t operation_id) override
    {
        last_operation = operation_id;
        last_target = turntable::LiftPosition::Raised;
        ++raises;
    }
    void lower(uint32_t operation_id) override
    {
        last_operation = operation_id;
        last_target = turntable::LiftPosition::Lowered;
        ++lowers;
    }

    turntable::LiftPosition last_target = turntable::LiftPosition::Unknown;
    uint32_t last_operation = 0;
    int raises = 0;
    int lowers = 0;
};

class FakeMaintenance final : public turntable::IMaintenanceExecutor {
public:
    bool start(turntable::MaintenanceOperation operation, uint32_t operation_id) override
    {
        last_operation_type = operation;
        last_operation_id = operation_id;
        ++starts;
        return accept;
    }
    void cancel() override { ++cancels; }
    bool cancellable() const override { return can_cancel; }

    turntable::MaintenanceOperation last_operation_type =
        turntable::MaintenanceOperation::PlatterAlignment;
    uint32_t last_operation_id = 0;
    bool accept = true;
    bool can_cancel = true;
    int starts = 0;
    int cancels = 0;
};

struct Rig {
    FakeClock clock;
    FakePlatter platter;
    FakeCarriage carriage;
    FakeLift lift;
    turntable::Controller controller;

    explicit Rig(turntable::ControllerConfig config = {})
        : controller(clock, platter, carriage, lift, config)
    {
    }

    void complete(turntable::EventType type)
    {
        controller.handle(turntable::Event::completion(type, controller.active_operation_id()));
    }

    void settle(turntable::LiftPosition position)
    {
        controller.handle(turntable::Event::lift_settled(
            controller.active_operation_id(), position, turntable::PositionConfidence::Estimated));
    }

    void initialize()
    {
        controller.handle(turntable::Event::simple(turntable::EventType::InitializeRequested));
        settle(turntable::LiftPosition::Raised);
        complete(turntable::EventType::HomeReferenceFound);
        complete(turntable::EventType::CarriageAtPark);
    }

    void start_playing()
    {
        controller.handle(turntable::Event::simple(turntable::EventType::PlayRequested));
        complete(turntable::EventType::PlatterSpeedLocked);
        complete(turntable::EventType::CarriageAtLeadIn);
        settle(turntable::LiftPosition::Lowered);
    }
};

class FakeDiagnosticExecutor final : public diagnostics::IExecutor {
public:
    bool start(const diagnostics::Command& value) override
    {
        command = value;
        report = {diagnostics::ExecutionState::Running};
        ++starts;
        return accept;
    }
    void abort() override
    {
        ++aborts;
        report = {diagnostics::ExecutionState::Idle};
    }
    void safe_stop() override
    {
        ++safe_stops;
        report = {diagnostics::ExecutionState::Idle};
    }
    diagnostics::ExecutionReport poll() override { return report; }

    diagnostics::Command command{};
    diagnostics::ExecutionReport report{};
    bool accept = true;
    int starts = 0;
    int aborts = 0;
    int safe_stops = 0;
};

void test_nominal_play_pause_speed_and_stop()
{
    Rig rig;
    CHECK(rig.controller.state() == turntable::State::NeedsHome);
    rig.initialize();
    CHECK(rig.controller.state() == turntable::State::Idle);
    CHECK(rig.carriage.home_confidence() == turntable::HomeConfidence::Valid);

    rig.start_playing();
    CHECK(rig.controller.state() == turntable::State::Playing);
    CHECK(rig.carriage.tracking);

    rig.controller.handle(turntable::Event::simple(turntable::EventType::PauseRequested));
    CHECK(rig.controller.state() == turntable::State::RaisingForPause);
    rig.settle(turntable::LiftPosition::Raised);
    CHECK(rig.controller.state() == turntable::State::Paused);
    CHECK(!rig.carriage.tracking);

    rig.controller.handle(turntable::Event::simple(turntable::EventType::ResumeRequested));
    rig.settle(turntable::LiftPosition::Lowered);
    CHECK(rig.controller.state() == turntable::State::Playing);

    rig.controller.handle(turntable::Event::speed_change(turntable::RecordSpeed::Rpm45));
    CHECK(rig.controller.state() == turntable::State::RaisingForSpeedChange);
    rig.settle(turntable::LiftPosition::Raised);
    CHECK(rig.controller.state() == turntable::State::ChangingSpeedForPlayback);
    CHECK(rig.platter.speed == turntable::RecordSpeed::Rpm45);
    rig.complete(turntable::EventType::PlatterSpeedLocked);
    rig.settle(turntable::LiftPosition::Lowered);
    CHECK(rig.controller.state() == turntable::State::Playing);

    rig.controller.handle(turntable::Event::simple(turntable::EventType::StopRequested));
    rig.settle(turntable::LiftPosition::Raised);
    rig.carriage.at_park_value = true;
    rig.complete(turntable::EventType::CarriageAtPark);
    rig.complete(turntable::EventType::PlatterStopped);
    CHECK(rig.controller.state() == turntable::State::Idle);
}

void test_stale_completion_is_ignored()
{
    Rig rig;
    rig.controller.handle(turntable::Event::simple(turntable::EventType::InitializeRequested));
    const uint32_t active = rig.controller.active_operation_id();
    rig.controller.handle(turntable::Event::lift_settled(
        active + 1, turntable::LiftPosition::Raised, turntable::PositionConfidence::Estimated));
    CHECK(rig.controller.state() == turntable::State::RaisingForInitialization);
    rig.controller.handle(turntable::Event::lift_settled(
        active, turntable::LiftPosition::Raised, turntable::PositionConfidence::Estimated));
    CHECK(rig.controller.state() == turntable::State::HomingCarriage);
}

void test_fault_recovery_preserves_valid_position()
{
    Rig rig;
    rig.initialize();
    rig.start_playing();
    rig.carriage.at_park_value = false;

    turntable::FaultRecord fault{turntable::FaultCode::PlatterDriver,
                                 turntable::FaultSource::Platter,
                                 turntable::RecoveryPolicy::Retryable, false, 12};
    rig.controller.handle(turntable::Event::detected_fault(fault));
    CHECK(rig.controller.state() == turntable::State::Fault);
    CHECK(rig.platter.emergency_stops == 1);
    rig.settle(turntable::LiftPosition::Raised);
    rig.controller.handle(turntable::Event::simple(turntable::EventType::FaultConditionCleared));
    rig.controller.handle(turntable::Event::simple(
        turntable::EventType::AcknowledgeFaultRequested));
    CHECK(rig.controller.state() == turntable::State::Interrupted);
}

void test_home_timeout_invalidates_home()
{
    turntable::ControllerConfig config;
    config.carriage_home_deadline_ms = 10;
    Rig rig(config);
    rig.controller.handle(turntable::Event::simple(turntable::EventType::InitializeRequested));
    rig.settle(turntable::LiftPosition::Raised);
    rig.clock.now = 11;
    rig.controller.tick();
    CHECK(rig.controller.state() == turntable::State::Fault);
    CHECK(rig.controller.snapshot().fault.code == turntable::FaultCode::CarriageHomeTimeout);
    CHECK(rig.carriage.home_confidence() == turntable::HomeConfidence::Unknown);
}

void test_diagnostic_lifecycle()
{
    FakeDiagnosticExecutor executor;
    diagnostics::Controller controller(executor);
    controller.enter();
    CHECK(controller.state() == diagnostics::State::Ready);

    diagnostics::Command command;
    command.target = diagnostics::Target::PlatterMotor;
    command.action = diagnostics::Action::OpenLoopSpin;
    CHECK(controller.submit(command));
    CHECK(controller.state() == diagnostics::State::Running);

    executor.report = {diagnostics::ExecutionState::Complete, 0, 1.0f, 2.0f, false};
    controller.tick();
    CHECK(controller.state() == diagnostics::State::Ready);

    CHECK(controller.submit(command));
    executor.report = {diagnostics::ExecutionState::Failed, -1, 0.0f, 0.0f, true};
    controller.tick();
    CHECK(controller.state() == diagnostics::State::Fault);
    CHECK(controller.home_invalidated());
    controller.acknowledge_fault();
    CHECK(controller.state() == diagnostics::State::Ready);

    controller.request_exit();
    controller.tick();
    CHECK(controller.state() == diagnostics::State::Inactive);
    CHECK(controller.exit_ready());
}

void test_application_authority_handoff()
{
    Rig rig;
    FakeDiagnosticExecutor executor;
    diagnostics::Controller diagnostics(executor);
    turntable::ApplicationController application(rig.controller, diagnostics);

    CHECK(application.request_diagnostic_entry());
    const uint32_t operation = rig.lift.last_operation;
    application.handle(turntable::Event::lift_settled(
        operation, turntable::LiftPosition::Raised, turntable::PositionConfidence::Estimated));
    CHECK(application.authority() == turntable::ControlAuthority::Diagnostic);
    CHECK(application.state() == turntable::ApplicationState::Diagnostic);

    application.request_diagnostic_exit();
    application.tick();
    CHECK(application.authority() == turntable::ControlAuthority::Normal);
    CHECK(application.state() == turntable::ApplicationState::Normal);
}

void test_period_rpm_estimator_and_lock_detector()
{
    platter_feedback::EstimatorConfig estimator_config;
    estimator_config.edge_filter_alpha = 1.0f;
    platter_feedback::RpmEstimator estimator(estimator_config);

    // 33 1/3 RPM at 4000 quadrature edges/rev is one edge every 450 us.
    estimator.on_edge(1000);
    estimator.on_edge(1450);
    CHECK(estimator.valid());
    CHECK(estimator.rpm() > 33.32f && estimator.rpm() < 33.35f);
    estimator.update(102000);
    CHECK(!estimator.valid());

    estimator.on_index(2000000);
    estimator.on_index(3800000); // 1.8 s/rev = 33 1/3 RPM
    CHECK(estimator.index_valid());
    CHECK(estimator.index_rpm() > 33.32f && estimator.index_rpm() < 33.35f);

    platter_feedback::LockConfig lock_config;
    lock_config.tolerance_rpm = 0.05f;
    lock_config.stable_time_ms = 1000;
    platter_feedback::SpeedLockDetector lock(lock_config);
    lock.set_target(33.3333f);
    CHECK(!lock.update(100, 33.34f, true));
    CHECK(!lock.update(1099, 33.33f, true));
    CHECK(lock.update(1100, 33.33f, true));
    CHECK(!lock.update(1101, 33.50f, true));
}

void test_maintenance_lifecycle()
{
    FakeClock clock;
    FakePlatter platter;
    FakeCarriage carriage;
    FakeLift lift;
    FakeMaintenance maintenance;
    turntable::Controller controller(clock, platter, carriage, lift, {}, &maintenance);

    controller.handle(turntable::Event::maintenance_request(
        turntable::MaintenanceOperation::EncoderCalibration));
    CHECK(controller.state() == turntable::State::Maintenance);
    CHECK(maintenance.starts == 1);
    CHECK(maintenance.last_operation_type
          == turntable::MaintenanceOperation::EncoderCalibration);
    controller.handle(turntable::Event::completion(
        turntable::EventType::MaintenanceCompleted, controller.active_operation_id()));
    CHECK(controller.state() == turntable::State::NeedsHome);

    controller.handle(turntable::Event::maintenance_request(
        turntable::MaintenanceOperation::PlatterAlignment));
    controller.handle(turntable::Event::simple(
        turntable::EventType::MaintenanceCancelRequested));
    CHECK(maintenance.cancels == 1);
    CHECK(controller.state() == turntable::State::NeedsHome);
}

}  // namespace

int main()
{
    test_nominal_play_pause_speed_and_stop();
    test_stale_completion_is_ignored();
    test_fault_recovery_preserves_valid_position();
    test_home_timeout_invalidates_home();
    test_diagnostic_lifecycle();
    test_application_authority_handoff();
    test_period_rpm_estimator_and_lock_detector();
    test_maintenance_lifecycle();

    if (failures == 0) {
        std::printf("All turntable tests passed.\n");
        return 0;
    }
    std::printf("%d test assertion(s) failed.\n", failures);
    return 1;
}
