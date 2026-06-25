#include "turntable/application_controller.hpp"
#include "control/platter_feedback.hpp"
#include "hmi/backlight_controller.hpp"
#include "hmi/interaction_controller.hpp"
#include "hmi/display_config.h"
#include "hmi/presenter.hpp"
#include "hmi/screenkey_demo.hpp"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                       \
            ++failures;                                                                             \
        }                                                                                           \
    } while (false)

#define CHECK_TEXT(actual, expected) CHECK(std::strcmp((actual), (expected)) == 0)

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

void test_abi_timer_estimator_and_speed_trace()
{
    platter_feedback::AbiEstimatorConfig config;
    config.filter_alpha = 1.0f;
    platter_feedback::AbiRpmEstimator estimator(config);

    platter_feedback::AbiTimerSample first;
    first.position_counts = 0xfffffff0u;
    first.timestamp_ticks = 0xfff00000u;
    estimator.on_sample(first);

    // 32 counts at 33 1/3 RPM take 14.4 ms, or 1,209,600 ticks at 84 MHz. Both
    // the quadrature counter and timebase intentionally cross their 32-bit rollover here.
    platter_feedback::AbiTimerSample second;
    second.position_counts = 0x00000010u;
    second.timestamp_ticks = first.timestamp_ticks + 1209600u;
    estimator.on_sample(second);
    CHECK(estimator.valid());
    CHECK(estimator.rpm() > 33.32f && estimator.rpm() < 33.35f);

    platter_feedback::AbiTimerSample index_one = second;
    index_one.index_sequence = 1;
    index_one.index_timestamp_ticks = 100000000u;
    estimator.on_sample(index_one);
    platter_feedback::AbiTimerSample index_two = index_one;
    index_two.index_sequence = 2;
    index_two.index_timestamp_ticks = index_one.index_timestamp_ticks + 151200000u;
    estimator.on_sample(index_two);
    CHECK(estimator.index_valid());
    CHECK(estimator.index_rpm() > 33.32f && estimator.index_rpm() < 33.35f);

    estimator.update(second.timestamp_ticks + config.timeout_ticks + 1u);
    CHECK(!estimator.valid());

    platter_feedback::SpeedTrace trace;
    trace.update(0, 33.333f, 33.343f, true);
    trace.update(50, 33.333f, 33.500f, true);
    trace.update(100, 33.333f, 33.313f, true);
    CHECK(trace.size() == 2);
    CHECK(trace.deviation_millirpm(0) == 10);
    CHECK(trace.deviation_millirpm(1) == -20);

    turntable::ApplicationSnapshot snapshot;
    snapshot.authority = turntable::ControlAuthority::Normal;
    snapshot.state = turntable::ApplicationState::Normal;
    snapshot.turntable.state = turntable::State::Playing;
    snapshot.turntable.actions.add(turntable::Action::SelectSpeed);
    const hmi::View view = hmi::present(snapshot, {}, 0, &trace);
    CHECK(view.keys[1].speed_sparkline.count == 2);
    CHECK(view.keys[1].speed_sparkline.samples[0] == 25);
    CHECK(view.keys[1].speed_sparkline.samples[1] == -50);

    snapshot.turntable.state = turntable::State::RaisingForPause;
    snapshot.turntable.actions = {};
    const hmi::View pausing = hmi::present(snapshot, {}, 0, &trace);
    CHECK(!pausing.keys[1].enabled);
    CHECK(pausing.keys[1].speed_sparkline.count == 2);
}

void test_backlight_inactivity_fade_and_wake()
{
    hmi::BacklightController backlight;
    backlight.reset(0);
    CHECK(backlight.duty() == 255);
    CHECK(!backlight.tick(5000).changed);

    hmi::BacklightUpdate update = backlight.tick(5010);
    CHECK(update.changed);
    CHECK(update.duty < 255);
    for (uint32_t now = 5020; now <= 9000; now += 10) {
        update = backlight.tick(now);
        CHECK(backlight.duty() >= 45);
    }
    CHECK(backlight.dimmed());
    CHECK(backlight.duty() == 45);

    backlight.note_activity(9000);
    for (uint32_t now = 9010; now <= 9400; now += 10) backlight.tick(now);
    CHECK(!backlight.dimmed());
    CHECK(backlight.duty() == 255);

    hmi::BacklightController wrapping;
    wrapping.reset(0xfffffff0u);
    CHECK(!wrapping.tick(0x00000020u).changed);
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

turntable::ApplicationSnapshot normal_snapshot(turntable::State state)
{
    turntable::ApplicationSnapshot snapshot;
    snapshot.authority = turntable::ControlAuthority::Normal;
    snapshot.state = turntable::ApplicationState::Normal;
    snapshot.turntable.state = state;
    return snapshot;
}

void test_screenkey_settings_navigation_and_guards()
{
    hmi::InteractionController interaction;
    turntable::ApplicationSnapshot snapshot = normal_snapshot(turntable::State::Idle);
    snapshot.turntable.actions.add(turntable::Action::OpenSettings);
    snapshot.turntable.actions.add(turntable::Action::EnterDiagnostics);

    CHECK(interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot).type
          == hmi::IntentType::None);
    CHECK(interaction.snapshot().mode == hmi::Mode::SettingsBrowse);
    CHECK(interaction.snapshot().settings_item == hmi::SettingsItem::SystemStatus);
    hmi::View view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "STATUS");

    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().settings_item == hmi::SettingsItem::Diagnostics);
    view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "DIAGNOSTIC");
    CHECK(view.keys[1].enabled);

    interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().mode == hmi::Mode::DiagnosticConfirmation);
    view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "ENTER");
    const hmi::Intent enter =
        interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(enter.type == hmi::IntentType::EnterDiagnostics);
    CHECK(interaction.snapshot().mode == hmi::Mode::Primary);

    hmi::InteractionController blocked;
    snapshot.turntable.actions = {};
    blocked.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    blocked.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    view = hmi::present(snapshot, blocked.snapshot());
    CHECK_TEXT(view.keys[1].detail, "STOP FIRST");
    CHECK(!view.keys[1].enabled);
    blocked.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(blocked.snapshot().mode == hmi::Mode::SettingsBrowse);

    blocked.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(blocked.snapshot().settings_item == hmi::SettingsItem::Brightness);
    blocked.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(blocked.snapshot().mode == hmi::Mode::Brightness);
    view = hmi::present(snapshot, blocked.snapshot());
    CHECK_TEXT(view.keys[2].action, "SMOOTH");
    CHECK(view.keys[2].enabled);
}

void test_screenkey_transport_and_global_stop()
{
    hmi::InteractionController interaction;
    turntable::ApplicationSnapshot snapshot = normal_snapshot(turntable::State::SpinningUpForPlay);
    snapshot.turntable.actions.add(turntable::Action::Stop);
    hmi::Intent intent =
        interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::TurntableEvent);
    CHECK(intent.event.type == turntable::EventType::StopRequested);

    snapshot.turntable.state = turntable::State::Playing;
    snapshot.turntable.actions.add(turntable::Action::Pause);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().mode == hmi::Mode::SettingsBrowse);
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Hold, snapshot);
    CHECK(intent.type == hmi::IntentType::TurntableEvent);
    CHECK(intent.event.type == turntable::EventType::StopRequested);
    CHECK(interaction.snapshot().mode == hmi::Mode::Primary);

    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.event.type == turntable::EventType::PauseRequested);

    const hmi::View playing = hmi::present(snapshot, {}, 17);
    CHECK(playing.keys[0].icon == hmi::IconId::Pause);
    CHECK(playing.keys[0].hold_available);
    CHECK(playing.keys[0].hold_progress == 17);

    turntable::ApplicationSnapshot idle = normal_snapshot(turntable::State::Idle);
    idle.turntable.actions.add(turntable::Action::Play);
    const hmi::View idle_view = hmi::present(idle);
    CHECK(idle_view.keys[0].icon == hmi::IconId::Play);
    CHECK(idle_view.keys[0].icon_color == kGreen);

    turntable::ApplicationSnapshot stopping =
        normal_snapshot(turntable::State::SpinningUpForPlay);
    stopping.turntable.actions.add(turntable::Action::Stop);
    const hmi::View stop_view = hmi::present(stopping);
    CHECK(stop_view.keys[0].icon == hmi::IconId::Stop);
    CHECK(stop_view.keys[0].icon_color == kRed);
}

void test_screenkey_fault_details_and_status_views()
{
    hmi::InteractionController interaction;
    turntable::ApplicationSnapshot snapshot = normal_snapshot(turntable::State::Fault);
    snapshot.turntable.actions.add(turntable::Action::AcknowledgeFault);
    snapshot.turntable.fault = {turntable::FaultCode::CarriageStall,
                                turntable::FaultSource::Carriage,
                                turntable::RecoveryPolicy::RequiresCarriageHome, true, 100};
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().mode == hmi::Mode::FaultDetails);
    hmi::View view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "CARRIAGE");
    CHECK(view.keys[1].icon == hmi::IconId::Warning);
    CHECK_TEXT(view.keys[1].detail, "STALL");
    CHECK_TEXT(view.keys[2].action, "REHOME");
    CHECK_TEXT(view.keys[2].detail, "HOME LOST");

    interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    const hmi::Intent acknowledge =
        interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(acknowledge.type == hmi::IntentType::TurntableEvent);
    CHECK(acknowledge.event.type == turntable::EventType::AcknowledgeFaultRequested);

    snapshot = normal_snapshot(turntable::State::Playing);
    snapshot.turntable.home = turntable::HomeConfidence::Valid;
    snapshot.turntable.selected_speed = turntable::RecordSpeed::Rpm33;
    snapshot.turntable.measured_rpm = 33.31f;
    hmi::InteractionController status;
    status.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    status.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    view = hmi::present(snapshot, status.snapshot());
    CHECK_TEXT(view.keys[1].action, "PLAYING");
    CHECK_TEXT(view.keys[2].action, "33 RPM");
    CHECK_TEXT(view.keys[2].detail, "33.31 RPM");
    /* No reading supplied -> SYSTEM detail is just the home string. */
    CHECK_TEXT(view.keys[1].detail, "HOME OK");
    /* Core temperature prefixes the SYSTEM detail when a reading is present. */
    view = hmi::present(snapshot, status.snapshot(), 0, nullptr, 42);
    CHECK_TEXT(view.keys[1].detail, "42C HOME OK");

    snapshot.turntable.selected_speed = turntable::RecordSpeed::Rpm45;
    snapshot.turntable.measured_rpm = 45.02f;
    view = hmi::present(snapshot);
    CHECK_TEXT(view.keys[1].action, "45 RPM");
    CHECK_TEXT(view.keys[1].detail, "45.02 RPM");
}

void test_screenkey_diagnostic_shortcuts()
{
    // config: open-loop, closed 33, closed 45, jog-step
    hmi::InteractionController interaction({1.25f, 3.5f, 7.0f, 1.0f});
    turntable::ApplicationSnapshot snapshot;
    snapshot.authority = turntable::ControlAuthority::Diagnostic;
    snapshot.state = turntable::ApplicationState::Diagnostic;
    snapshot.diagnostic.state = diagnostics::State::Ready;

    // Diagnostics open on the target browser.
    CHECK(interaction.snapshot().diag_page == hmi::DiagPage::TargetBrowser);
    hmi::View view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "PLATTER");

    // KEY2 cycles the target; KEY1 selects it -> that motor's test browser.
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_target == hmi::DiagTarget::Tonearm);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_target == hmi::DiagTarget::Platter);
    interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_page == hmi::DiagPage::TestBrowser);
    CHECK(interaction.snapshot().diag_test == 0);

    // Platter list: SPIN(0), ALIGN(1), AUTO-CAL(2), VELOCITY(3). KEY2 scrolls, KEY1 runs.
    view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "SPIN");
    hmi::Intent intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::SubmitDiagnostic);
    CHECK(intent.diagnostic.target == diagnostics::Target::PlatterMotor);
    CHECK(intent.diagnostic.action == diagnostics::Action::OpenLoopSpin);
    CHECK(intent.diagnostic.parameters.value == 1.25f);

    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // -> ALIGN
    CHECK(interaction.snapshot().diag_test == 1);
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::ElectricalAlign);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // -> AUTO-CAL
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::EncoderAutoCal);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // -> VELOCITY
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::ClosedLoopVelocity);
    CHECK(intent.diagnostic.parameters.value == 3.5f);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // wraps back to SPIN
    CHECK(interaction.snapshot().diag_test == 0);

    // KEY0 backs out to the target browser; again on the browser exits.
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::None);
    CHECK(interaction.snapshot().diag_page == hmi::DiagPage::TargetBrowser);
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::ExitDiagnostics);

    // A running test aborts on KEY0 tap (re-enter the test browser first).
    interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    snapshot.diagnostic.state = diagnostics::State::Running;
    snapshot.diagnostic.command.action = diagnostics::Action::OpenLoopSpin;
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::AbortDiagnostic);

    // Fault handling is page-independent.
    snapshot.diagnostic.state = diagnostics::State::Fault;
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::AcknowledgeDiagnosticFault);
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Hold, snapshot);
    CHECK(intent.type == hmi::IntentType::ExitDiagnostics);
    view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[0].action, "ACK");
}

void test_screenkey_diagnostic_tonearm_jog()
{
    hmi::InteractionController interaction({1.25f, 3.5f, 7.0f, 1.0f}); // jog step = 1.0
    turntable::ApplicationSnapshot snapshot;
    snapshot.authority = turntable::ControlAuthority::Diagnostic;
    snapshot.state = turntable::ApplicationState::Diagnostic;
    snapshot.diagnostic.state = diagnostics::State::Ready;

    // Browser -> select TONEARM -> its test browser.
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_target == hmi::DiagTarget::Tonearm);
    interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_page == hmi::DiagPage::TestBrowser);

    // Tonearm list: ALIGN(0), JOG-(1), JOG+(2), SPIN(3). KEY1 runs the selected test.
    hmi::View view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].action, "ALIGN");
    hmi::Intent intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.target == diagnostics::Target::TonearmCarriage);
    CHECK(intent.diagnostic.action == diagnostics::Action::ElectricalAlign);

    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // -> JOG -
    CHECK_TEXT(hmi::present(snapshot, interaction.snapshot()).keys[1].action, "JOG -");
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::Jog);
    CHECK(intent.diagnostic.parameters.value == -1.0f);

    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // -> JOG +
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::Jog);
    CHECK(intent.diagnostic.parameters.value == 1.0f);

    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot); // -> SPIN
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.target == diagnostics::Target::TonearmCarriage);
    CHECK(intent.diagnostic.action == diagnostics::Action::OpenLoopSpin);
    CHECK(intent.diagnostic.parameters.value == 1.25f);

    // Running open-loop spin: KEY0 = STOP, and the running detail shows the live pole estimate.
    snapshot.diagnostic.state = diagnostics::State::Running;
    snapshot.diagnostic.command.action = diagnostics::Action::OpenLoopSpin;
    snapshot.diagnostic.report.primary = 11.0f;
    view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[0].action, "STOP");
    CHECK_TEXT(view.keys[1].detail, "POLES=11.00");
    intent = interaction.handle(hmi::Key::Transport, hmi::Gesture::Tap, snapshot);
    CHECK(intent.type == hmi::IntentType::AbortDiagnostic);
}

void test_screenkey_demo_walkthrough()
{
    hmi::ScreenKeyDemo demo({1.0f, 2.0f});
    demo.reset(0);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::NeedsHome);

    demo.handle(hmi::Key::Transport, hmi::Gesture::Tap, 0);
    CHECK(demo.application_snapshot().turntable.state
          == turntable::State::RaisingForInitialization);
    demo.tick(600);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::HomingCarriage);
    demo.tick(1800);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::ParkingCarriage);
    demo.tick(2400);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::Idle);
    CHECK(demo.application_snapshot().turntable.home == turntable::HomeConfidence::Valid);

    demo.handle(hmi::Key::Transport, hmi::Gesture::Tap, 2400);
    demo.tick(3400);
    demo.tick(4000);
    demo.tick(4600);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::Playing);
    const hmi::View ripple_view = hmi::present(
        demo.application_snapshot(), demo.navigation_snapshot(), 0, &demo.speed_trace());
    CHECK(ripple_view.keys[1].speed_sparkline.count >= 2);

    demo.handle(hmi::Key::Settings, hmi::Gesture::Tap, 4600);
    CHECK(demo.navigation_snapshot().mode == hmi::Mode::SettingsBrowse);
    demo.handle(hmi::Key::Transport, hmi::Gesture::Hold, 4600);
    CHECK(demo.navigation_snapshot().mode == hmi::Mode::Primary);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::RaisingForStop);
    demo.tick(5200);
    demo.tick(6000);
    demo.tick(6600);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::Idle);

    demo.handle(hmi::Key::Settings, hmi::Gesture::Hold, 6600);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::Fault);
    CHECK(demo.application_snapshot().turntable.fault.invalidates_home);
    demo.handle(hmi::Key::Settings, hmi::Gesture::Tap, 6600);
    CHECK(demo.navigation_snapshot().mode == hmi::Mode::FaultDetails);
    demo.handle(hmi::Key::Transport, hmi::Gesture::Tap, 6600);
    demo.handle(hmi::Key::Transport, hmi::Gesture::Tap, 6600);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::NeedsHome);
}

void test_screenkey_demo_diagnostic_shortcuts()
{
    FakeDiagnosticExecutor executor;
    diagnostics::Controller controller(executor);
    hmi::ScreenKeyDemo demo({1.0f, 2.0f, 3.0f}, &controller);
    demo.reset(0);
    demo.handle(hmi::Key::Settings, hmi::Gesture::Tap, 0);
    demo.handle(hmi::Key::Settings, hmi::Gesture::Tap, 0);
    demo.handle(hmi::Key::Speed, hmi::Gesture::Tap, 0);
    CHECK(demo.navigation_snapshot().mode == hmi::Mode::DiagnosticConfirmation);
    demo.handle(hmi::Key::Speed, hmi::Gesture::Tap, 0);
    CHECK(demo.application_snapshot().state
          == turntable::ApplicationState::EnteringDiagnostic);
    demo.tick(600);
    CHECK(demo.application_snapshot().authority == turntable::ControlAuthority::Diagnostic);
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Ready);

    // Diagnostics open on the target browser; KEY1 selects PLATTER -> its test browser, KEY1 again
    // runs the first test (SPIN) on the real diagnostic controller.
    demo.handle(hmi::Key::Speed, hmi::Gesture::Tap, 600);
    CHECK(demo.navigation_snapshot().diag_page == hmi::DiagPage::TestBrowser);
    demo.handle(hmi::Key::Speed, hmi::Gesture::Tap, 600);
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Running);
    CHECK(demo.application_snapshot().diagnostic.command.action
          == diagnostics::Action::OpenLoopSpin);
    CHECK(executor.starts == 1);
    demo.handle(hmi::Key::Transport, hmi::Gesture::Hold, 600);
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Stopping);
    demo.tick(1100);
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Ready);

    // A failed execution report surfaces as a diagnostic fault, then acknowledges back to ready.
    demo.handle(hmi::Key::Speed, hmi::Gesture::Tap, 1100); // re-run the selected test (SPIN)
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Running);
    executor.report = {diagnostics::ExecutionState::Failed, -1};
    demo.tick(1100);
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Fault);
    demo.handle(hmi::Key::Transport, hmi::Gesture::Tap, 1100);
    CHECK(demo.application_snapshot().diagnostic.state == diagnostics::State::Ready);
    // KEY0 hold backs out to the browser, then a second hold exits diagnostics.
    demo.handle(hmi::Key::Transport, hmi::Gesture::Hold, 1100);
    CHECK(demo.navigation_snapshot().diag_page == hmi::DiagPage::TargetBrowser);
    demo.handle(hmi::Key::Transport, hmi::Gesture::Hold, 1100);
    demo.tick(1600);
    CHECK(demo.application_snapshot().authority == turntable::ControlAuthority::Normal);
    CHECK(demo.application_snapshot().turntable.state == turntable::State::NeedsHome);
}

void test_screenkey_diagnostic_speed_target()
{
    hmi::InteractionController interaction({1.0f, 2.0f, 3.0f});
    turntable::ApplicationSnapshot snapshot;
    snapshot.authority = turntable::ControlAuthority::Diagnostic;
    snapshot.state = turntable::ApplicationState::Diagnostic;
    snapshot.diagnostic.state = diagnostics::State::Ready;

    // Reach the platter test browser and scroll to VELOCITY (SPIN, ALIGN, AUTO-CAL, VELOCITY).
    interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_page == hmi::DiagPage::TestBrowser);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    interaction.handle(hmi::Key::Settings, hmi::Gesture::Tap, snapshot);
    CHECK(interaction.snapshot().diag_test == 3);

    // The closed-loop spin target follows whichever record speed was selected.
    snapshot.turntable.selected_speed = turntable::RecordSpeed::Rpm33;
    hmi::Intent intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::ClosedLoopVelocity);
    CHECK(intent.diagnostic.parameters.value == 2.0f);

    snapshot.turntable.selected_speed = turntable::RecordSpeed::Rpm45;
    intent = interaction.handle(hmi::Key::Speed, hmi::Gesture::Tap, snapshot);
    CHECK(intent.diagnostic.action == diagnostics::Action::ClosedLoopVelocity);
    CHECK(intent.diagnostic.parameters.value == 3.0f);

    // The running detail reports closed-loop vs open-loop fallback while the velocity loop runs.
    snapshot.diagnostic.state = diagnostics::State::Running;
    snapshot.diagnostic.command.action = diagnostics::Action::ClosedLoopVelocity;
    snapshot.diagnostic.report.platter_calibrated = true;
    hmi::View view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].detail, "CLOSED LOOP");
    snapshot.diagnostic.report.platter_calibrated = false;
    view = hmi::present(snapshot, interaction.snapshot());
    CHECK_TEXT(view.keys[1].detail, "OPEN: RUN CAL");
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
    test_abi_timer_estimator_and_speed_trace();
    test_backlight_inactivity_fade_and_wake();
    test_maintenance_lifecycle();
    test_screenkey_settings_navigation_and_guards();
    test_screenkey_transport_and_global_stop();
    test_screenkey_fault_details_and_status_views();
    test_screenkey_diagnostic_shortcuts();
    test_screenkey_diagnostic_tonearm_jog();
    test_screenkey_diagnostic_speed_target();
    test_screenkey_demo_walkthrough();
    test_screenkey_demo_diagnostic_shortcuts();

    if (failures == 0) {
        std::printf("All turntable tests passed.\n");
        return 0;
    }
    std::printf("%d test assertion(s) failed.\n", failures);
    return 1;
}
