#include "screenkey_demo.hpp"

namespace hmi {
namespace {

float selected_rpm(turntable::RecordSpeed speed)
{
    return speed == turntable::RecordSpeed::Rpm45 ? 45.0f : 33.3333f;
}

bool is_playback_state(turntable::State state)
{
    using turntable::State;
    switch (state) {
    case State::SpinningUpForPlay:
    case State::SeekingLeadIn:
    case State::LoweringForPlay:
    case State::Playing:
    case State::RaisingForPause:
    case State::Paused:
    case State::LoweringForResume:
    case State::RaisingForSpeedChange:
    case State::ChangingSpeedForPlayback:
    case State::LoweringAfterSpeedChange:
    case State::ChangingSpeedWhilePaused:
    case State::RaisingForStop:
    case State::ReturningHomeWithPlatter:
    case State::StoppingPlatter:
    case State::SpinningUpForResume:
        return true;
    default:
        return false;
    }
}

}  // namespace

void ScreenKeyDemo::reset(uint32_t now_ms)
{
    interaction_ = InteractionController(interaction_config_);
    application_ = {};
    application_.authority = turntable::ControlAuthority::Normal;
    application_.state = turntable::ApplicationState::Normal;
    application_.turntable.selected_speed = turntable::RecordSpeed::Rpm33;
    application_.turntable.home = turntable::HomeConfidence::Unknown;
    application_.turntable.lift.position = turntable::LiftPosition::Unknown;
    application_.turntable.lift.confidence = turntable::PositionConfidence::Unknown;
    deadline_ = Deadline::None;
    deadline_started_ms_ = now_ms;
    deadline_duration_ms_ = 0;
    set_state(turntable::State::NeedsHome);
}

void ScreenKeyDemo::handle(Key key, Gesture gesture, uint32_t now_ms)
{
    interaction_.synchronize(application_);
    if (key == Key::Settings && gesture == Gesture::Hold) {
        if (application_.authority == turntable::ControlAuthority::Diagnostic
            && application_.diagnostic.state == diagnostics::State::Ready) {
            inject_diagnostic_fault();
            return;
        }
        if (application_.authority == turntable::ControlAuthority::Normal
            && interaction_.snapshot().mode == Mode::Primary) {
            inject_product_fault();
            return;
        }
    }
    apply(interaction_.handle(key, gesture, application_), now_ms);
}

void ScreenKeyDemo::tick(uint32_t now_ms)
{
    if (deadline_ == Deadline::None
        || static_cast<uint32_t>(now_ms - deadline_started_ms_) < deadline_duration_ms_)
        return;
    const Deadline elapsed = deadline_;
    deadline_ = Deadline::None;
    advance(elapsed, now_ms);
    interaction_.synchronize(application_);
}

void ScreenKeyDemo::apply(const Intent& intent, uint32_t now_ms)
{
    switch (intent.type) {
    case IntentType::None:
        break;
    case IntentType::TurntableEvent:
        apply_turntable_event(intent.event, now_ms);
        break;
    case IntentType::EnterDiagnostics:
        application_.state = turntable::ApplicationState::EnteringDiagnostic;
        application_.turntable.lift.motion = turntable::MotionState::Moving;
        schedule(Deadline::FinishDiagnosticEntry, 600, now_ms);
        break;
    case IntentType::ExitDiagnostics:
        application_.state = turntable::ApplicationState::ExitingDiagnostic;
        application_.diagnostic.state = diagnostics::State::Stopping;
        schedule(Deadline::FinishDiagnosticExit, 500, now_ms);
        break;
    case IntentType::SubmitDiagnostic:
        application_.diagnostic.command = intent.diagnostic;
        application_.diagnostic.report = {diagnostics::ExecutionState::Running};
        application_.diagnostic.state = diagnostics::State::Running;
        schedule(Deadline::FinishDiagnosticCommand, 1200, now_ms);
        break;
    case IntentType::AbortDiagnostic:
        application_.diagnostic.state = diagnostics::State::Stopping;
        schedule(Deadline::FinishDiagnosticAbort, 500, now_ms);
        break;
    case IntentType::AcknowledgeDiagnosticFault:
        application_.diagnostic.report = {};
        application_.diagnostic.state = diagnostics::State::Ready;
        break;
    }
}

void ScreenKeyDemo::apply_turntable_event(const turntable::Event& event, uint32_t now_ms)
{
    using turntable::EventType;
    using turntable::State;
    switch (event.type) {
    case EventType::InitializeRequested:
        set_state(State::RaisingForInitialization);
        schedule(Deadline::BeginHoming, 600, now_ms);
        break;
    case EventType::CancelRequested:
        deadline_ = Deadline::None;
        set_state(State::NeedsHome);
        break;
    case EventType::PlayRequested:
        set_state(State::SpinningUpForPlay);
        schedule(Deadline::BeginSeeking, 1000, now_ms);
        break;
    case EventType::PauseRequested:
        set_state(State::RaisingForPause);
        schedule(Deadline::FinishPause, 600, now_ms);
        break;
    case EventType::ResumeRequested:
        set_state(State::LoweringForResume);
        schedule(Deadline::FinishResume, 600, now_ms);
        break;
    case EventType::StopRequested:
        if (application_.turntable.state == State::Interrupted) {
            set_state(State::ReturningHomeWithoutPlatter);
            schedule(Deadline::FinishReturnWithoutPlatter, 800, now_ms);
        } else {
            set_state(State::RaisingForStop);
            schedule(Deadline::BeginReturn, 600, now_ms);
        }
        break;
    case EventType::SpeedChangeRequested:
        application_.turntable.selected_speed = event.speed;
        if (application_.turntable.state == State::Playing) {
            set_state(State::RaisingForSpeedChange);
            schedule(Deadline::BeginSpeedChange, 600, now_ms);
        } else if (application_.turntable.state == State::Paused) {
            set_state(State::ChangingSpeedWhilePaused);
            schedule(Deadline::FinishSpeedChange, 800, now_ms);
        } else {
            update_actions();
        }
        break;
    case EventType::AcknowledgeFaultRequested:
        application_.turntable.fault = {};
        set_state(application_.turntable.home == turntable::HomeConfidence::Valid
                      ? State::Interrupted : State::NeedsHome);
        break;
    case EventType::MaintenanceCancelRequested:
        set_state(application_.turntable.home == turntable::HomeConfidence::Valid
                      ? State::Idle : State::NeedsHome);
        break;
    default:
        break;
    }
}

void ScreenKeyDemo::set_state(turntable::State state)
{
    application_.turntable.state = state;
    using turntable::LiftPosition;
    using turntable::MotionState;
    using turntable::State;
    switch (state) {
    case State::RaisingForInitialization:
    case State::RaisingForPause:
    case State::RaisingForSpeedChange:
    case State::RaisingForStop:
        application_.turntable.lift.motion = MotionState::Moving;
        break;
    case State::LoweringForPlay:
    case State::LoweringForResume:
    case State::LoweringAfterSpeedChange:
        application_.turntable.lift.motion = MotionState::Moving;
        break;
    case State::Playing:
        application_.turntable.lift.position = LiftPosition::Lowered;
        application_.turntable.lift.motion = MotionState::Idle;
        application_.turntable.measured_rpm = selected_rpm(application_.turntable.selected_speed);
        application_.turntable.speed_locked = true;
        break;
    case State::Paused:
        application_.turntable.lift.position = LiftPosition::Raised;
        application_.turntable.lift.motion = MotionState::Idle;
        application_.turntable.measured_rpm = selected_rpm(application_.turntable.selected_speed);
        application_.turntable.speed_locked = true;
        break;
    case State::Idle:
    case State::NeedsHome:
        application_.turntable.lift.position = LiftPosition::Raised;
        application_.turntable.lift.motion = MotionState::Idle;
        application_.turntable.measured_rpm = 0.0f;
        application_.turntable.speed_locked = false;
        break;
    case State::Fault:
        application_.turntable.lift.position = LiftPosition::Raised;
        application_.turntable.lift.motion = MotionState::Idle;
        application_.turntable.measured_rpm = 0.0f;
        application_.turntable.speed_locked = false;
        break;
    default:
        if (is_playback_state(state))
            application_.turntable.measured_rpm = selected_rpm(
                application_.turntable.selected_speed);
        break;
    }
    update_actions();
}

void ScreenKeyDemo::update_actions()
{
    application_.turntable.actions = {};
    turntable::ActionSet& actions = application_.turntable.actions;
    actions.add(turntable::Action::OpenSettings);
    using turntable::Action;
    using turntable::State;
    switch (application_.turntable.state) {
    case State::NeedsHome:
        actions.add(Action::Initialize);
        actions.add(Action::SelectSpeed);
        actions.add(Action::EnterDiagnostics);
        break;
    case State::RaisingForInitialization:
    case State::HomingCarriage:
    case State::ParkingCarriage:
        actions.add(Action::Cancel);
        break;
    case State::Idle:
        actions.add(Action::Play);
        actions.add(Action::SelectSpeed);
        actions.add(Action::EnterDiagnostics);
        break;
    case State::Playing:
        actions.add(Action::Pause);
        actions.add(Action::Stop);
        actions.add(Action::SelectSpeed);
        break;
    case State::Paused:
    case State::Interrupted:
        actions.add(Action::Resume);
        actions.add(Action::Stop);
        actions.add(Action::SelectSpeed);
        break;
    case State::Fault:
        actions.add(Action::AcknowledgeFault);
        actions.add(Action::FaultDetails);
        break;
    case State::Maintenance:
        actions.add(Action::Cancel);
        break;
    case State::StoppingPlatter:
        break;
    default:
        actions.add(Action::Stop);
        break;
    }
}

void ScreenKeyDemo::schedule(Deadline deadline, uint32_t duration_ms, uint32_t now_ms)
{
    deadline_ = deadline;
    deadline_started_ms_ = now_ms;
    deadline_duration_ms_ = duration_ms;
}

void ScreenKeyDemo::advance(Deadline deadline, uint32_t now_ms)
{
    using turntable::State;
    switch (deadline) {
    case Deadline::None:
        break;
    case Deadline::BeginHoming:
        set_state(State::HomingCarriage);
        schedule(Deadline::BeginParking, 1200, now_ms);
        break;
    case Deadline::BeginParking:
        application_.turntable.carriage_position_mm = 0.0f;
        set_state(State::ParkingCarriage);
        schedule(Deadline::FinishInitialization, 600, now_ms);
        break;
    case Deadline::FinishInitialization:
        application_.turntable.home = turntable::HomeConfidence::Valid;
        application_.turntable.carriage_position_mm = 3.0f;
        application_.turntable.lift.confidence = turntable::PositionConfidence::Estimated;
        set_state(State::Idle);
        break;
    case Deadline::BeginSeeking:
        application_.turntable.measured_rpm = selected_rpm(application_.turntable.selected_speed);
        application_.turntable.speed_locked = true;
        set_state(State::SeekingLeadIn);
        schedule(Deadline::BeginLoweringForPlay, 600, now_ms);
        break;
    case Deadline::BeginLoweringForPlay:
        application_.turntable.carriage_position_mm = 8.0f;
        set_state(State::LoweringForPlay);
        schedule(Deadline::FinishPlay, 600, now_ms);
        break;
    case Deadline::FinishPlay:
        set_state(State::Playing);
        break;
    case Deadline::FinishPause:
        set_state(State::Paused);
        break;
    case Deadline::FinishResume:
        set_state(State::Playing);
        break;
    case Deadline::BeginSpeedChange:
        application_.turntable.lift.position = turntable::LiftPosition::Raised;
        set_state(State::ChangingSpeedForPlayback);
        schedule(Deadline::BeginLoweringAfterSpeedChange, 800, now_ms);
        break;
    case Deadline::BeginLoweringAfterSpeedChange:
        application_.turntable.measured_rpm = selected_rpm(application_.turntable.selected_speed);
        application_.turntable.lift.position = turntable::LiftPosition::Lowered;
        set_state(State::LoweringAfterSpeedChange);
        schedule(Deadline::FinishSpeedChange, 600, now_ms);
        break;
    case Deadline::FinishSpeedChange:
        set_state(application_.turntable.lift.position == turntable::LiftPosition::Raised
                      ? State::Paused : State::Playing);
        break;
    case Deadline::BeginReturn:
        application_.turntable.lift.position = turntable::LiftPosition::Raised;
        set_state(State::ReturningHomeWithPlatter);
        schedule(Deadline::BeginPlatterStop, 800, now_ms);
        break;
    case Deadline::BeginPlatterStop:
        application_.turntable.carriage_position_mm = 3.0f;
        set_state(State::StoppingPlatter);
        schedule(Deadline::FinishStop, 600, now_ms);
        break;
    case Deadline::FinishStop:
    case Deadline::FinishReturnWithoutPlatter:
        application_.turntable.carriage_position_mm = 3.0f;
        set_state(State::Idle);
        break;
    case Deadline::FinishDiagnosticEntry:
        application_.turntable.lift.position = turntable::LiftPosition::Raised;
        application_.turntable.lift.motion = turntable::MotionState::Idle;
        application_.authority = turntable::ControlAuthority::Diagnostic;
        application_.state = turntable::ApplicationState::Diagnostic;
        application_.diagnostic = {};
        application_.diagnostic.state = diagnostics::State::Ready;
        break;
    case Deadline::FinishDiagnosticCommand:
        application_.diagnostic.report.state = diagnostics::ExecutionState::Complete;
        application_.diagnostic.state = diagnostics::State::Ready;
        break;
    case Deadline::FinishDiagnosticAbort:
        application_.diagnostic.report = {};
        application_.diagnostic.state = diagnostics::State::Ready;
        break;
    case Deadline::FinishDiagnosticExit:
        application_.authority = turntable::ControlAuthority::Normal;
        application_.state = turntable::ApplicationState::Normal;
        application_.diagnostic = {};
        set_state(application_.turntable.home == turntable::HomeConfidence::Valid
                      ? State::Idle : State::NeedsHome);
        break;
    }
}

void ScreenKeyDemo::inject_product_fault()
{
    deadline_ = Deadline::None;
    application_.turntable.home = turntable::HomeConfidence::Unknown;
    application_.turntable.fault = {
        turntable::FaultCode::CarriageStall, turntable::FaultSource::Carriage,
        turntable::RecoveryPolicy::RequiresCarriageHome, true, 0};
    set_state(turntable::State::Fault);
    interaction_.synchronize(application_);
}

void ScreenKeyDemo::inject_diagnostic_fault()
{
    deadline_ = Deadline::None;
    application_.diagnostic.report = {
        diagnostics::ExecutionState::Failed, -1, 0.0f, 0.0f, false};
    application_.diagnostic.state = diagnostics::State::Fault;
}

}  // namespace hmi
