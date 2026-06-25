#include "interaction_controller.hpp"

namespace hmi {
namespace {

Intent turntable_event(const turntable::Event& event)
{
    Intent intent;
    intent.type = IntentType::TurntableEvent;
    intent.event = event;
    return intent;
}

Intent simple_intent(IntentType type)
{
    Intent intent;
    intent.type = type;
    return intent;
}

Intent diagnostic_intent(diagnostics::Target target, diagnostics::Action action, float value = 0.0f)
{
    Intent intent;
    intent.type = IntentType::SubmitDiagnostic;
    intent.diagnostic.target = target;
    intent.diagnostic.action = action;
    intent.diagnostic.parameters.value = value;
    return intent;
}

bool diagnostic_running(const diagnostics::Snapshot& snapshot, diagnostics::Action action)
{
    return snapshot.state == diagnostics::State::Running && snapshot.command.action == action;
}

}  // namespace

Intent InteractionController::handle(Key key, Gesture gesture,
                                     const turntable::ApplicationSnapshot& application)
{
    synchronize(application);
    if (gesture == Gesture::None) return {};
    if (application.authority == turntable::ControlAuthority::Diagnostic)
        return handle_diagnostic(key, gesture, application.diagnostic,
                                 application.turntable.selected_speed);
    return handle_normal(key, gesture, application);
}

void InteractionController::synchronize(const turntable::ApplicationSnapshot& application)
{
    if (application.authority == turntable::ControlAuthority::Diagnostic
        || application.state != turntable::ApplicationState::Normal) {
        mode_ = Mode::Primary;
        return;
    }
    /* Outside diagnostics: keep the diagnostic browser reset so each entry starts on the target
     * browser at the platter target. */
    diag_page_ = DiagPage::TargetBrowser;
    diag_target_ = DiagTarget::Platter;
    if (application.turntable.state == turntable::State::Fault
        && mode_ != Mode::FaultDetails)
        mode_ = Mode::Primary;
}

Intent InteractionController::handle_normal(
    Key key, Gesture gesture, const turntable::ApplicationSnapshot& application)
{
    const turntable::Snapshot& state = application.turntable;

    if (key == Key::Transport && gesture == Gesture::Hold) {
        mode_ = Mode::Primary;
        return global_stop(state);
    }

    if (mode_ == Mode::Primary) return handle_primary(key, gesture, application);

    if (mode_ == Mode::SettingsBrowse) {
        if (key == Key::Transport && gesture == Gesture::Tap) {
            mode_ = Mode::Primary;
        } else if (key == Key::Settings && gesture == Gesture::Tap) {
            next_settings_item();
        } else if (key == Key::Speed && gesture == Gesture::Tap) {
            switch (settings_item_) {
            case SettingsItem::SystemStatus:
                mode_ = Mode::SystemStatus;
                break;
            case SettingsItem::Diagnostics:
                if (state.actions.contains(turntable::Action::EnterDiagnostics))
                    mode_ = Mode::DiagnosticConfirmation;
                break;
            case SettingsItem::Brightness:
                mode_ = Mode::Brightness;
                break;
            }
        }
        return {};
    }

    if (mode_ == Mode::DiagnosticConfirmation) {
        if (key == Key::Transport && gesture == Gesture::Tap) {
            mode_ = Mode::SettingsBrowse;
        } else if (key == Key::Speed && gesture == Gesture::Tap
                   && state.actions.contains(turntable::Action::EnterDiagnostics)) {
            mode_ = Mode::Primary;
            return simple_intent(IntentType::EnterDiagnostics);
        }
        return {};
    }

    if (key == Key::Transport && gesture == Gesture::Tap) {
        mode_ = mode_ == Mode::FaultDetails ? Mode::Primary : Mode::SettingsBrowse;
    }
    return {};
}

Intent InteractionController::handle_primary(
    Key key, Gesture gesture, const turntable::ApplicationSnapshot& application)
{
    const turntable::Snapshot& state = application.turntable;
    if (application.state == turntable::ApplicationState::EnteringDiagnostic) {
        if (key == Key::Transport && gesture == Gesture::Tap)
            return turntable_event(turntable::Event::simple(turntable::EventType::CancelRequested));
        return {};
    }

    if (gesture != Gesture::Tap) return {};
    if (key == Key::Transport) {
        if (state.actions.contains(turntable::Action::Initialize))
            return turntable_event(
                turntable::Event::simple(turntable::EventType::InitializeRequested));
        if (state.actions.contains(turntable::Action::Play))
            return turntable_event(turntable::Event::simple(turntable::EventType::PlayRequested));
        if (state.actions.contains(turntable::Action::Pause))
            return turntable_event(turntable::Event::simple(turntable::EventType::PauseRequested));
        if (state.actions.contains(turntable::Action::Resume))
            return turntable_event(turntable::Event::simple(turntable::EventType::ResumeRequested));
        if (state.actions.contains(turntable::Action::AcknowledgeFault))
            return turntable_event(
                turntable::Event::simple(turntable::EventType::AcknowledgeFaultRequested));
        if (state.actions.contains(turntable::Action::Cancel)) {
            const turntable::EventType type = state.state == turntable::State::Maintenance
                ? turntable::EventType::MaintenanceCancelRequested
                : turntable::EventType::CancelRequested;
            return turntable_event(turntable::Event::simple(type));
        }
        if (state.actions.contains(turntable::Action::Stop))
            return turntable_event(turntable::Event::simple(turntable::EventType::StopRequested));
        return {};
    }

    if (key == Key::Speed && state.actions.contains(turntable::Action::SelectSpeed)) {
        const turntable::RecordSpeed next = state.selected_speed == turntable::RecordSpeed::Rpm33
            ? turntable::RecordSpeed::Rpm45 : turntable::RecordSpeed::Rpm33;
        return turntable_event(turntable::Event::speed_change(next));
    }

    if (key == Key::Settings) {
        mode_ = state.state == turntable::State::Fault ? Mode::FaultDetails
                                                       : Mode::SettingsBrowse;
    }
    return {};
}

Intent InteractionController::handle_diagnostic(Key key, Gesture gesture,
                                                const diagnostics::Snapshot& diagnostic,
                                                turntable::RecordSpeed selected_speed)
{
    /* Transport hold is the universal navigate-up/stop: abort a running command, else back out of a
     * test page to the target browser, else (already on the browser) exit diagnostic authority. */
    if (key == Key::Transport && gesture == Gesture::Hold) {
        if (diagnostic.state == diagnostics::State::Running)
            return simple_intent(IntentType::AbortDiagnostic);
        if (diagnostic.state == diagnostics::State::Fault)
            return simple_intent(IntentType::ExitDiagnostics);
        if (diag_page_ != DiagPage::TargetBrowser) {
            diag_page_ = DiagPage::TargetBrowser;
            return {};
        }
        return simple_intent(IntentType::ExitDiagnostics);
    }

    if (gesture != Gesture::Tap && gesture != Gesture::Hold) return {};
    if (diagnostic.state == diagnostics::State::Fault) {
        if (key == Key::Transport && gesture == Gesture::Tap)
            return simple_intent(IntentType::AcknowledgeDiagnosticFault);
        return {};
    }
    if (diagnostic.state == diagnostics::State::Stopping
        || diagnostic.state == diagnostics::State::Inactive)
        return {};

    switch (diag_page_) {
    case DiagPage::TargetBrowser:
        return handle_target_browser(key, gesture);
    case DiagPage::PlatterTests:
        return handle_platter_tests(key, gesture, diagnostic, selected_speed);
    case DiagPage::TonearmTests:
        return handle_tonearm_tests(key, gesture, diagnostic);
    }
    return {};
}

Intent InteractionController::handle_target_browser(Key key, Gesture gesture)
{
    if (gesture != Gesture::Tap) return {};
    if (key == Key::Transport) return simple_intent(IntentType::ExitDiagnostics);
    if (key == Key::Speed) { /* select the current target -> enter its test page */
        diag_page_ = diag_target_ == DiagTarget::Platter ? DiagPage::PlatterTests
                                                         : DiagPage::TonearmTests;
        return {};
    }
    if (key == Key::Settings) /* next target */
        diag_target_ = diag_target_ == DiagTarget::Platter ? DiagTarget::Tonearm
                                                           : DiagTarget::Platter;
    return {};
}

Intent InteractionController::handle_platter_tests(Key key, Gesture gesture,
                                                   const diagnostics::Snapshot& diagnostic,
                                                   turntable::RecordSpeed selected_speed)
{
    using diagnostics::Action;
    using diagnostics::Target;
    if (key == Key::Transport && gesture == Gesture::Tap) {
        if (diagnostic_running(diagnostic, Action::OpenLoopSpin))
            return simple_intent(IntentType::AbortDiagnostic);
        if (diagnostic.state == diagnostics::State::Ready)
            return diagnostic_intent(Target::PlatterMotor, Action::OpenLoopSpin,
                                     config_.diagnostic_open_loop_velocity);
    }

    if (key == Key::Speed) {
        if (diagnostic.state != diagnostics::State::Ready) return {};
        if (gesture == Gesture::Hold)
            return diagnostic_intent(Target::PlatterMotor, Action::EncoderAutoCal);
        return diagnostic_intent(Target::PlatterMotor, Action::ElectricalAlign);
    }

    if (key == Key::Settings && gesture == Gesture::Tap) {
        if (diagnostic_running(diagnostic, Action::ClosedLoopVelocity))
            return simple_intent(IntentType::AbortDiagnostic);
        if (diagnostic.state == diagnostics::State::Ready) {
            /* Spin the platter at the motor speed for whichever record speed was selected before
             * entering diagnostics, scaled by the belt ratio. */
            const float velocity = selected_speed == turntable::RecordSpeed::Rpm45
                ? config_.diagnostic_closed_loop_velocity_45
                : config_.diagnostic_closed_loop_velocity;
            return diagnostic_intent(Target::PlatterMotor, Action::ClosedLoopVelocity, velocity);
        }
    }
    return {};
}

Intent InteractionController::handle_tonearm_tests(Key key, Gesture gesture,
                                                   const diagnostics::Snapshot& diagnostic)
{
    using diagnostics::Action;
    using diagnostics::Target;
    const bool jogging = diagnostic_running(diagnostic, Action::Jog);

    /* Jog the carriage a fixed motor angle: KEY0 = back, KEY2 = forward, KEY1 = auto-align. */
    if (key == Key::Transport && gesture == Gesture::Tap) {
        if (jogging) return simple_intent(IntentType::AbortDiagnostic);
        if (diagnostic.state == diagnostics::State::Ready)
            return diagnostic_intent(Target::TonearmCarriage, Action::Jog,
                                     -config_.diagnostic_jog_step_rad);
    }
    if (key == Key::Speed && gesture == Gesture::Tap) {
        if (diagnostic.state == diagnostics::State::Ready)
            return diagnostic_intent(Target::TonearmCarriage, Action::ElectricalAlign);
    }
    if (key == Key::Settings && gesture == Gesture::Tap) {
        if (jogging) return simple_intent(IntentType::AbortDiagnostic);
        if (diagnostic.state == diagnostics::State::Ready)
            return diagnostic_intent(Target::TonearmCarriage, Action::Jog,
                                     config_.diagnostic_jog_step_rad);
    }
    return {};
}

Intent InteractionController::global_stop(const turntable::Snapshot& state)
{
    if (state.actions.contains(turntable::Action::Stop))
        return turntable_event(turntable::Event::simple(turntable::EventType::StopRequested));
    return {};
}

void InteractionController::next_settings_item()
{
    switch (settings_item_) {
    case SettingsItem::SystemStatus:
        settings_item_ = SettingsItem::Diagnostics;
        break;
    case SettingsItem::Diagnostics:
        settings_item_ = SettingsItem::Brightness;
        break;
    case SettingsItem::Brightness:
        settings_item_ = SettingsItem::SystemStatus;
        break;
    }
}

}  // namespace hmi
