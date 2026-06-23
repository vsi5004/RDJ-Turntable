#include "controller.hpp"

namespace turntable {

Controller::Controller(IClock& clock, IPlatter& platter, ITonearmCarriage& carriage,
                       ITonearmLift& lift, ControllerConfig config,
                       IMaintenanceExecutor* maintenance)
    : clock_(clock), platter_(platter), carriage_(carriage), lift_(lift),
      maintenance_(maintenance), config_(config)
{
}

void Controller::handle(const Event& event)
{
    if (event.type == EventType::FaultDetected) {
        if (state_ != State::Fault) enter_fault(event.fault);
        return;
    }

    if (event.type == EventType::FaultConditionCleared) {
        fault_condition_clear_ = true;
        return;
    }

    if (state_ == State::Fault) {
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Raised) {
            fault_raise_settled_ = true;
            clear_deadline();
        } else if (event.type == EventType::AcknowledgeFaultRequested
                   && fault_condition_clear_ && fault_raise_settled_
                   && fault_.recovery != RecoveryPolicy::RequiresPowerCycle) {
            fault_ = {};
            if (carriage_.home_confidence() == HomeConfidence::Unknown)
                transition_to(State::NeedsHome);
            else if (carriage_.at_park())
                transition_to(State::Idle);
            else
                transition_to(State::Interrupted);
        } else if (event.type == EventType::DeadlineExpired && matches_active(event)) {
            // PWM lift completion is time-based in the initial implementation.
            fault_raise_settled_ = true;
            clear_deadline();
        }
        return;
    }

    if (event.type == EventType::DeadlineExpired) {
        if (matches_active(event)) handle_deadline();
        return;
    }

    if (event.type == EventType::StopRequested) {
        handle_stop();
        return;
    }

    if (event.type == EventType::SpeedChangeRequested) {
        handle_speed_change(event.speed);
        return;
    }

    if (event.type == EventType::MaintenanceRequested) {
        if ((state_ == State::Idle || state_ == State::NeedsHome) && maintenance_ != nullptr) {
            maintenance_operation_ = event.maintenance;
            transition_to(State::Maintenance);
        }
        return;
    }

    if (event.type == EventType::PlatterSpeedLost) {
        switch (state_) {
        case State::Playing:
        case State::Paused:
        case State::LoweringForResume:
        case State::RaisingForPause:
        case State::RaisingForSpeedChange:
        case State::ChangingSpeedForPlayback:
        case State::LoweringAfterSpeedChange:
        case State::ChangingSpeedWhilePaused:
            enter_fault({FaultCode::PlatterLockTimeout, FaultSource::Platter,
                         RecoveryPolicy::Retryable, false, clock_.now_ms()});
            return;
        default:
            break;
        }
    }

    switch (state_) {
    case State::NeedsHome:
        if (event.type == EventType::InitializeRequested)
            transition_to(State::RaisingForInitialization);
        break;

    case State::RaisingForInitialization:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Raised)
            transition_to(State::HomingCarriage);
        else if (event.type == EventType::CancelRequested) {
            carriage_.stop();
            lift_.raise(++next_operation_id_);
            transition_to(State::NeedsHome);
        }
        break;

    case State::HomingCarriage:
        if (event.type == EventType::HomeReferenceFound && matches_active(event)) {
            carriage_.establish_home_reference();
            transition_to(State::ParkingCarriage);
        } else if (event.type == EventType::CancelRequested) {
            carriage_.stop();
            carriage_.invalidate_home();
            lift_.raise(++next_operation_id_);
            transition_to(State::NeedsHome);
        }
        break;

    case State::ParkingCarriage:
        if (event.type == EventType::CarriageAtPark && matches_active(event)) {
            carriage_.mark_home_valid();
            transition_to(State::Idle);
        } else if (event.type == EventType::CancelRequested) {
            carriage_.stop();
            carriage_.invalidate_home();
            lift_.raise(++next_operation_id_);
            transition_to(State::NeedsHome);
        }
        break;

    case State::Idle:
        if (event.type == EventType::PlayRequested
            && carriage_.home_confidence() == HomeConfidence::Valid)
            transition_to(State::SpinningUpForPlay);
        break;

    case State::SpinningUpForPlay:
        if (event.type == EventType::PlatterSpeedLocked && matches_active(event))
            transition_to(State::SeekingLeadIn);
        break;

    case State::SeekingLeadIn:
        if (event.type == EventType::CarriageAtLeadIn && matches_active(event))
            transition_to(State::LoweringForPlay);
        break;

    case State::LoweringForPlay:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Lowered)
            transition_to(State::Playing);
        break;

    case State::Playing:
        if (event.type == EventType::PauseRequested)
            transition_to(State::RaisingForPause);
        else if (event.type == EventType::EndOfSideDetected)
            transition_to(State::RaisingForStop);
        break;

    case State::RaisingForPause:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Raised)
            transition_to(State::Paused);
        break;

    case State::Paused:
        if (event.type == EventType::ResumeRequested)
            transition_to(State::LoweringForResume);
        break;

    case State::LoweringForResume:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Lowered)
            transition_to(State::Playing);
        break;

    case State::RaisingForSpeedChange:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Raised)
            transition_to(State::ChangingSpeedForPlayback);
        break;

    case State::ChangingSpeedForPlayback:
        if (event.type == EventType::PlatterSpeedLocked && matches_active(event))
            transition_to(State::LoweringAfterSpeedChange);
        break;

    case State::LoweringAfterSpeedChange:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Lowered)
            transition_to(State::Playing);
        break;

    case State::ChangingSpeedWhilePaused:
        if (event.type == EventType::PlatterSpeedLocked && matches_active(event))
            transition_to(State::Paused);
        break;

    case State::RaisingForStop:
        if (event.type == EventType::LiftSettled && matches_active(event)
            && event.lift_position == LiftPosition::Raised)
            transition_to(State::ReturningHomeWithPlatter);
        break;

    case State::ReturningHomeWithPlatter:
        if (event.type == EventType::CarriageAtPark && matches_active(event))
            transition_to(State::StoppingPlatter);
        break;

    case State::StoppingPlatter:
        if (event.type == EventType::PlatterStopped && matches_active(event)) {
            if (carriage_.home_confidence() == HomeConfidence::Valid)
                transition_to(State::Idle);
            else
                transition_to(State::NeedsHome);
        }
        break;

    case State::Interrupted:
        if (event.type == EventType::ResumeRequested)
            transition_to(State::SpinningUpForResume);
        break;

    case State::SpinningUpForResume:
        if (event.type == EventType::PlatterSpeedLocked && matches_active(event))
            transition_to(State::LoweringForResume);
        break;

    case State::ReturningHomeWithoutPlatter:
        if (event.type == EventType::CarriageAtPark && matches_active(event))
            transition_to(State::Idle);
        break;

    case State::Maintenance:
        if (event.type == EventType::MaintenanceCompleted && matches_active(event)) {
            if (carriage_.home_confidence() == HomeConfidence::Valid && carriage_.at_park())
                transition_to(State::Idle);
            else
                transition_to(State::NeedsHome);
        } else if (event.type == EventType::MaintenanceCancelRequested && maintenance_ != nullptr
                   && maintenance_->cancellable()) {
            maintenance_->cancel();
            if (carriage_.home_confidence() == HomeConfidence::Valid && carriage_.at_park())
                transition_to(State::Idle);
            else
                transition_to(State::NeedsHome);
        }
        break;
    case State::Fault:
        break;
    }
}

void Controller::tick()
{
    if (!deadline_armed_) return;
    if (static_cast<uint32_t>(clock_.now_ms() - deadline_started_ms_) < deadline_duration_ms_)
        return;

    Event event = Event::completion(EventType::DeadlineExpired, active_operation_id_);
    handle(event);
}

void Controller::update_observations(const Observations& observations)
{
    observations_ = observations;
}

Snapshot Controller::snapshot() const
{
    Snapshot out;
    out.state = state_;
    out.selected_speed = selected_speed_;
    out.measured_rpm = observations_.measured_rpm;
    out.speed_locked = observations_.speed_locked;
    out.home = carriage_.home_confidence();
    out.carriage_position_mm = observations_.carriage_position_mm;
    out.lift = observations_.lift;
    out.fault = fault_;
    out.actions = available_actions();
    return out;
}

bool Controller::can_enter_diagnostics() const
{
    return state_ == State::Idle || state_ == State::NeedsHome;
}

uint32_t Controller::prepare_for_diagnostics()
{
    platter_.emergency_stop();
    carriage_.set_tracking(false);
    carriage_.stop();
    clear_deadline();
    active_operation_id_ = ++next_operation_id_;
    if (active_operation_id_ == 0) active_operation_id_ = ++next_operation_id_;
    lift_.raise(active_operation_id_);
    return active_operation_id_;
}

void Controller::restore_after_diagnostics(bool invalidate_home)
{
    if (invalidate_home) carriage_.invalidate_home();
    fault_ = {};
    fault_condition_clear_ = false;
    fault_raise_settled_ = false;
    if (carriage_.home_confidence() == HomeConfidence::Valid && carriage_.at_park())
        transition_to(State::Idle);
    else
        transition_to(State::NeedsHome);
}

void Controller::transition_to(State next)
{
    if (state_ == State::Playing && next != State::Playing)
        carriage_.set_tracking(false);
    clear_deadline();
    state_ = next;
    on_enter(next);
}

void Controller::on_enter(State state)
{
    switch (state) {
    case State::NeedsHome:
    case State::Idle:
    case State::Paused:
    case State::Interrupted:
        break;
    case State::Maintenance:
        if (maintenance_ == nullptr
            || !maintenance_->start(maintenance_operation_,
                                    begin_operation(config_.maintenance_deadline_ms))) {
            enter_fault({FaultCode::MaintenanceFailed, FaultSource::Product,
                         RecoveryPolicy::Retryable, false, clock_.now_ms()});
        }
        break;
    case State::RaisingForInitialization:
    case State::RaisingForPause:
    case State::RaisingForSpeedChange:
    case State::RaisingForStop:
        lift_.raise(begin_operation(config_.lift_raise_deadline_ms));
        break;
    case State::HomingCarriage:
        carriage_.home(begin_operation(config_.carriage_home_deadline_ms));
        break;
    case State::ParkingCarriage:
    case State::ReturningHomeWithPlatter:
    case State::ReturningHomeWithoutPlatter:
        carriage_.move_to_park(begin_operation(config_.carriage_move_deadline_ms));
        break;
    case State::SpinningUpForPlay:
    case State::SpinningUpForResume:
    case State::ChangingSpeedForPlayback:
    case State::ChangingSpeedWhilePaused:
        platter_.start(selected_speed_, begin_operation(config_.platter_lock_deadline_ms));
        break;
    case State::SeekingLeadIn:
        carriage_.move_to_lead_in(begin_operation(config_.carriage_move_deadline_ms));
        break;
    case State::LoweringForPlay:
    case State::LoweringForResume:
    case State::LoweringAfterSpeedChange:
        lift_.lower(begin_operation(config_.lift_lower_deadline_ms));
        break;
    case State::Playing:
        carriage_.set_tracking(true);
        break;
    case State::StoppingPlatter:
        platter_.stop(begin_operation(config_.platter_stop_deadline_ms));
        break;
    case State::Fault:
        platter_.emergency_stop();
        carriage_.set_tracking(false);
        carriage_.stop();
        fault_raise_settled_ = false;
        lift_.raise(begin_operation(config_.lift_raise_deadline_ms));
        break;
    }
}

void Controller::handle_stop()
{
    switch (state_) {
    case State::SpinningUpForPlay:
        transition_to(State::StoppingPlatter);
        break;
    case State::SeekingLeadIn:
        carriage_.stop();
        transition_to(State::ReturningHomeWithPlatter);
        break;
    case State::LoweringForPlay:
    case State::Playing:
    case State::LoweringForResume:
    case State::LoweringAfterSpeedChange:
        transition_to(State::RaisingForStop);
        break;
    case State::RaisingForPause:
    case State::RaisingForSpeedChange:
        state_ = State::RaisingForStop; // retain the in-flight raise and operation ID
        break;
    case State::Paused:
    case State::SpinningUpForResume:
    case State::ChangingSpeedWhilePaused:
    case State::ChangingSpeedForPlayback:
        transition_to(State::ReturningHomeWithPlatter);
        break;
    case State::Interrupted:
        transition_to(State::ReturningHomeWithoutPlatter);
        break;
    default:
        break;
    }
}

void Controller::handle_speed_change(RecordSpeed speed)
{
    if (speed == selected_speed_) return;

    switch (state_) {
    case State::NeedsHome:
    case State::Idle:
    case State::Interrupted:
        selected_speed_ = speed;
        break;
    case State::Playing:
        selected_speed_ = speed;
        transition_to(State::RaisingForSpeedChange);
        break;
    case State::Paused:
        selected_speed_ = speed;
        transition_to(State::ChangingSpeedWhilePaused);
        break;
    default:
        break;
    }
}

void Controller::handle_deadline()
{
    if (state_ == State::Fault) {
        fault_raise_settled_ = true;
        clear_deadline();
        return;
    }
    enter_fault(deadline_fault());
}

void Controller::enter_fault(FaultRecord fault)
{
    if (fault.code == FaultCode::None) fault.code = FaultCode::Unknown;
    if (fault.occurred_at_ms == 0) fault.occurred_at_ms = clock_.now_ms();
    fault_ = fault;
    fault_condition_clear_ = false;
    if (fault.invalidates_home) carriage_.invalidate_home();
    transition_to(State::Fault);
}

FaultRecord Controller::deadline_fault() const
{
    FaultRecord fault;
    fault.occurred_at_ms = clock_.now_ms();
    switch (state_) {
    case State::SpinningUpForPlay:
    case State::SpinningUpForResume:
    case State::ChangingSpeedForPlayback:
    case State::ChangingSpeedWhilePaused:
        fault = {FaultCode::PlatterLockTimeout, FaultSource::Platter,
                 RecoveryPolicy::Retryable, false, fault.occurred_at_ms};
        break;
    case State::HomingCarriage:
        fault = {FaultCode::CarriageHomeTimeout, FaultSource::Carriage,
                 RecoveryPolicy::RequiresCarriageHome, true, fault.occurred_at_ms};
        break;
    case State::SeekingLeadIn:
        fault = {FaultCode::CarriageSeekTimeout, FaultSource::Carriage,
                 RecoveryPolicy::RequiresCarriageHome, true, fault.occurred_at_ms};
        break;
    case State::ParkingCarriage:
    case State::ReturningHomeWithPlatter:
    case State::ReturningHomeWithoutPlatter:
        fault = {FaultCode::CarriageReturnTimeout, FaultSource::Carriage,
                 RecoveryPolicy::RequiresCarriageHome, true, fault.occurred_at_ms};
        break;
    case State::Maintenance:
        fault = {FaultCode::MaintenanceTimeout, FaultSource::Product,
                 RecoveryPolicy::Retryable, false, fault.occurred_at_ms};
        break;
    default:
        fault = {FaultCode::Unknown, FaultSource::Product,
                 RecoveryPolicy::Retryable, false, fault.occurred_at_ms};
        break;
    }
    return fault;
}

uint32_t Controller::begin_operation(uint32_t timeout_ms)
{
    active_operation_id_ = ++next_operation_id_;
    if (active_operation_id_ == 0) active_operation_id_ = ++next_operation_id_;
    deadline_started_ms_ = clock_.now_ms();
    deadline_duration_ms_ = timeout_ms;
    deadline_armed_ = timeout_ms != 0;
    return active_operation_id_;
}

bool Controller::matches_active(const Event& event) const
{
    return event.operation_id != 0 && event.operation_id == active_operation_id_;
}

void Controller::clear_deadline()
{
    deadline_armed_ = false;
    deadline_duration_ms_ = 0;
}

ActionSet Controller::available_actions() const
{
    ActionSet actions;
    actions.add(Action::OpenSettings);
    switch (state_) {
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
        actions.add(Action::Resume);
        actions.add(Action::Stop);
        actions.add(Action::SelectSpeed);
        break;
    case State::Interrupted:
        actions.add(Action::Resume);
        actions.add(Action::Stop);
        actions.add(Action::SelectSpeed);
        break;
    case State::Fault:
        actions.add(Action::FaultDetails);
        if (fault_condition_clear_ && fault_raise_settled_
            && fault_.recovery != RecoveryPolicy::RequiresPowerCycle)
            actions.add(Action::AcknowledgeFault);
        break;
    case State::SpinningUpForPlay:
    case State::SeekingLeadIn:
    case State::LoweringForPlay:
    case State::RaisingForPause:
    case State::LoweringForResume:
    case State::RaisingForSpeedChange:
    case State::ChangingSpeedForPlayback:
    case State::LoweringAfterSpeedChange:
    case State::ChangingSpeedWhilePaused:
    case State::RaisingForStop:
    case State::ReturningHomeWithPlatter:
    case State::SpinningUpForResume:
    case State::ReturningHomeWithoutPlatter:
        actions.add(Action::Stop);
        break;
    case State::StoppingPlatter:
        break;
    case State::Maintenance:
        if (maintenance_ != nullptr && maintenance_->cancellable()) actions.add(Action::Cancel);
        break;
    }
    return actions;
}

}  // namespace turntable
