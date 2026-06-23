#include "diagnostic_controller.hpp"

namespace diagnostics {

void Controller::enter()
{
    executor_.safe_stop();
    command_ = {};
    report_ = {};
    exit_ready_ = false;
    home_invalidated_ = false;
    state_ = State::Ready;
}

bool Controller::submit(Command command)
{
    if (state_ != State::Ready) return false;
    command.command_id = ++next_command_id_;
    if (command.command_id == 0) command.command_id = ++next_command_id_;
    if (!executor_.start(command)) return false;

    command_ = command;
    report_ = {ExecutionState::Running};
    state_ = State::Running;
    return true;
}

void Controller::abort()
{
    if (state_ != State::Running) return;
    stop_intent_ = StopIntent::AbortCommand;
    executor_.abort();
    state_ = State::Stopping;
}

void Controller::request_exit()
{
    if (state_ == State::Inactive) {
        exit_ready_ = true;
        return;
    }
    stop_intent_ = StopIntent::ExitMode;
    executor_.safe_stop();
    state_ = State::Stopping;
}

void Controller::acknowledge_fault()
{
    if (state_ != State::Fault) return;
    executor_.safe_stop();
    report_ = {};
    state_ = State::Ready;
}

void Controller::tick()
{
    if (state_ != State::Running && state_ != State::Stopping) return;
    report_ = executor_.poll();

    if (state_ == State::Running) {
        if (report_.state == ExecutionState::Complete) {
            home_invalidated_ = home_invalidated_ || report_.invalidates_home;
            state_ = State::Ready;
        } else if (report_.state == ExecutionState::Failed) {
            home_invalidated_ = home_invalidated_ || report_.invalidates_home;
            executor_.safe_stop();
            state_ = State::Fault;
        }
        return;
    }

    if (report_.state != ExecutionState::Idle && report_.state != ExecutionState::Complete
        && report_.state != ExecutionState::Failed)
        return;

    home_invalidated_ = home_invalidated_ || report_.invalidates_home;
    if (stop_intent_ == StopIntent::ExitMode) {
        state_ = State::Inactive;
        exit_ready_ = true;
    } else {
        state_ = State::Ready;
    }
}

Snapshot Controller::snapshot() const
{
    return Snapshot{state_, command_, report_, exit_ready_};
}

}  // namespace diagnostics
