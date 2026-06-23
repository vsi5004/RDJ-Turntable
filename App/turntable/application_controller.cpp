#include "application_controller.hpp"

namespace turntable {

bool ApplicationController::request_diagnostic_entry()
{
    if (state_ != ApplicationState::Normal || !normal_.can_enter_diagnostics()) return false;
    entry_operation_id_ = normal_.prepare_for_diagnostics();
    state_ = ApplicationState::EnteringDiagnostic;
    return true;
}

void ApplicationController::boot_diagnostics()
{
    if (state_ != ApplicationState::Normal) return;
    normal_.prepare_for_diagnostics();
    diagnostic_.enter();
    authority_ = ControlAuthority::Diagnostic;
    state_ = ApplicationState::Diagnostic;
}

void ApplicationController::request_diagnostic_exit()
{
    if (state_ != ApplicationState::Diagnostic) return;
    diagnostic_.request_exit();
    state_ = ApplicationState::ExitingDiagnostic;
}

bool ApplicationController::submit_diagnostic(diagnostics::Command command)
{
    return state_ == ApplicationState::Diagnostic && diagnostic_.submit(command);
}

void ApplicationController::abort_diagnostic()
{
    if (state_ == ApplicationState::Diagnostic) diagnostic_.abort();
}

void ApplicationController::acknowledge_diagnostic_fault()
{
    if (state_ == ApplicationState::Diagnostic) diagnostic_.acknowledge_fault();
}

void ApplicationController::handle(const Event& event)
{
    if (state_ == ApplicationState::EnteringDiagnostic) {
        if (event.type == EventType::LiftSettled && event.operation_id == entry_operation_id_
            && event.lift_position == LiftPosition::Raised) {
            diagnostic_.enter();
            authority_ = ControlAuthority::Diagnostic;
            state_ = ApplicationState::Diagnostic;
        } else if (event.type == EventType::CancelRequested) {
            normal_.restore_after_diagnostics();
            state_ = ApplicationState::Normal;
        } else if (event.type == EventType::FaultDetected) {
            normal_.handle(event);
            state_ = ApplicationState::Normal;
        }
        return;
    }

    if (authority_ == ControlAuthority::Normal) normal_.handle(event);
}

void ApplicationController::tick()
{
    Event event;
    while (events_.pop(event)) handle(event);

    if (authority_ == ControlAuthority::Normal) normal_.tick();
    else diagnostic_.tick();

    if (state_ == ApplicationState::ExitingDiagnostic && diagnostic_.exit_ready()) {
        authority_ = ControlAuthority::Normal;
        normal_.restore_after_diagnostics(diagnostic_.home_invalidated());
        state_ = ApplicationState::Normal;
    }
}

ApplicationSnapshot ApplicationController::snapshot() const
{
    return ApplicationSnapshot{authority_, state_, normal_.snapshot(), diagnostic_.snapshot()};
}

}  // namespace turntable
