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

/* How a test's command parameter is derived (the action alone does not say sign / which speed). */
enum class TestParam : uint8_t { None, OpenLoopVel, JogBack, JogForward, Velocity };

struct DiagTestDef {
    const char* label;
    diagnostics::Target target;
    diagnostics::Action action;
    TestParam param;
};

constexpr DiagTestDef kPlatterTests[] = {
    {"SPIN", diagnostics::Target::PlatterMotor, diagnostics::Action::OpenLoopSpin,
     TestParam::OpenLoopVel},
    {"ALIGN", diagnostics::Target::PlatterMotor, diagnostics::Action::ElectricalAlign,
     TestParam::None},
    {"AUTO-CAL", diagnostics::Target::PlatterMotor, diagnostics::Action::EncoderAutoCal,
     TestParam::None},
    {"VELOCITY", diagnostics::Target::PlatterMotor, diagnostics::Action::ClosedLoopVelocity,
     TestParam::Velocity},
};

constexpr DiagTestDef kTonearmTests[] = {
    {"ALIGN", diagnostics::Target::TonearmCarriage, diagnostics::Action::ElectricalAlign,
     TestParam::None},
    {"JOG -", diagnostics::Target::TonearmCarriage, diagnostics::Action::Jog, TestParam::JogBack},
    {"JOG +", diagnostics::Target::TonearmCarriage, diagnostics::Action::Jog, TestParam::JogForward},
    {"SPIN", diagnostics::Target::TonearmCarriage, diagnostics::Action::OpenLoopSpin,
     TestParam::OpenLoopVel},
};

const DiagTestDef* tests_for(DiagTarget target, uint8_t& count)
{
    if (target == DiagTarget::Tonearm) {
        count = static_cast<uint8_t>(sizeof(kTonearmTests) / sizeof(kTonearmTests[0]));
        return kTonearmTests;
    }
    count = static_cast<uint8_t>(sizeof(kPlatterTests) / sizeof(kPlatterTests[0]));
    return kPlatterTests;
}

}  // namespace

uint8_t diag_test_count(DiagTarget target)
{
    uint8_t count = 0;
    tests_for(target, count);
    return count;
}

const char* diag_test_label(DiagTarget target, uint8_t index)
{
    uint8_t count = 0;
    const DiagTestDef* tests = tests_for(target, count);
    return index < count ? tests[index].label : "";
}

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
    diag_test_ = 0;
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
    case DiagPage::TestBrowser:
        return handle_test_browser(key, gesture, diagnostic, selected_speed);
    }
    return {};
}

Intent InteractionController::handle_target_browser(Key key, Gesture gesture)
{
    if (gesture != Gesture::Tap) return {};
    if (key == Key::Transport) return simple_intent(IntentType::ExitDiagnostics); // back == exit
    if (key == Key::Speed) { /* select the current target -> its test browser */
        diag_page_ = DiagPage::TestBrowser;
        diag_test_ = 0;
        return {};
    }
    if (key == Key::Settings) /* next target */
        diag_target_ = diag_target_ == DiagTarget::Platter ? DiagTarget::Tonearm
                                                           : DiagTarget::Platter;
    return {};
}

Intent InteractionController::handle_test_browser(Key key, Gesture gesture,
                                                  const diagnostics::Snapshot& diagnostic,
                                                  turntable::RecordSpeed selected_speed)
{
    if (gesture != Gesture::Tap) return {};

    /* While a test runs, the only tap action is stop (KEY0); transport-hold also aborts globally. */
    if (diagnostic.state == diagnostics::State::Running) {
        if (key == Key::Transport) return simple_intent(IntentType::AbortDiagnostic);
        return {};
    }

    if (key == Key::Transport) { /* back to the target browser */
        diag_page_ = DiagPage::TargetBrowser;
        return {};
    }
    if (key == Key::Settings) { /* scroll to the next test */
        const uint8_t count = diag_test_count(diag_target_);
        if (count != 0) diag_test_ = static_cast<uint8_t>((diag_test_ + 1) % count);
        return {};
    }
    if (key == Key::Speed) return run_selected_test(selected_speed); /* run the selected test */
    return {};
}

Intent InteractionController::run_selected_test(turntable::RecordSpeed selected_speed)
{
    uint8_t count = 0;
    const DiagTestDef* tests = tests_for(diag_target_, count);
    if (diag_test_ >= count) return {};
    const DiagTestDef& test = tests[diag_test_];

    float value = 0.0f;
    switch (test.param) {
    case TestParam::None:
        break;
    case TestParam::OpenLoopVel:
        value = config_.diagnostic_open_loop_velocity;
        break;
    case TestParam::JogBack:
        value = -config_.diagnostic_jog_step_rad;
        break;
    case TestParam::JogForward:
        value = config_.diagnostic_jog_step_rad;
        break;
    case TestParam::Velocity:
        /* Spin the platter at the motor speed for whichever record speed was selected, belt-scaled. */
        value = selected_speed == turntable::RecordSpeed::Rpm45
            ? config_.diagnostic_closed_loop_velocity_45
            : config_.diagnostic_closed_loop_velocity;
        break;
    }
    return diagnostic_intent(test.target, test.action, value);
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
