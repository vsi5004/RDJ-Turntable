#pragma once

#include "turntable/application_controller.hpp"

#include <cstdint>

namespace hmi {

enum class Key : uint8_t { Transport, Speed, Settings };
enum class Gesture : uint8_t { None, Tap, Hold };

enum class Mode : uint8_t {
    Primary,
    SettingsBrowse,
    SystemStatus,
    DiagnosticConfirmation,
    Brightness,
    FaultDetails,
};

enum class SettingsItem : uint8_t { SystemStatus, Diagnostics, Brightness };

/* Diagnostic navigation (per docs): a target browser selects a motor, then a test browser scrolls
 * that motor's tests (align / jog / spin / ...) and runs the selected one. */
enum class DiagPage : uint8_t { TargetBrowser, TestBrowser };
enum class DiagTarget : uint8_t { Platter, Tonearm };

/* Per-target diagnostic test list, shared by the controller (builds the command) and the presenter
 * (renders the names). Index order is the scroll order. */
uint8_t diag_test_count(DiagTarget target);
const char* diag_test_label(DiagTarget target, uint8_t index);

enum class IntentType : uint8_t {
    None,
    TurntableEvent,
    EnterDiagnostics,
    ExitDiagnostics,
    SubmitDiagnostic,
    AbortDiagnostic,
    AcknowledgeDiagnosticFault,
};

struct Intent {
    IntentType type = IntentType::None;
    turntable::Event event{};
    diagnostics::Command diagnostic{};
};

struct InteractionConfig {
    float diagnostic_open_loop_velocity = 0.0f;
    float diagnostic_closed_loop_velocity = 0.0f;    // motor target for 33 1/3 RPM playback
    float diagnostic_closed_loop_velocity_45 = 0.0f; // motor target for 45 RPM playback
    float diagnostic_jog_step_rad = 0.0f;            // tonearm carriage jog step (motor rad)
};

struct NavigationSnapshot {
    Mode mode = Mode::Primary;
    SettingsItem settings_item = SettingsItem::SystemStatus;
    DiagPage diag_page = DiagPage::TargetBrowser;
    DiagTarget diag_target = DiagTarget::Platter;
    uint8_t diag_test = 0; // selected test index within the current target's list
};

class InteractionController {
public:
    explicit InteractionController(InteractionConfig config = {}) : config_(config) {}

    Intent handle(Key key, Gesture gesture, const turntable::ApplicationSnapshot& application);
    void synchronize(const turntable::ApplicationSnapshot& application);
    NavigationSnapshot snapshot() const
    {
        return {mode_, settings_item_, diag_page_, diag_target_, diag_test_};
    }

private:
    Intent handle_normal(Key key, Gesture gesture,
                         const turntable::ApplicationSnapshot& application);
    Intent handle_primary(Key key, Gesture gesture,
                          const turntable::ApplicationSnapshot& application);
    Intent handle_diagnostic(Key key, Gesture gesture, const diagnostics::Snapshot& diagnostic,
                             turntable::RecordSpeed selected_speed);
    Intent handle_target_browser(Key key, Gesture gesture);
    Intent handle_test_browser(Key key, Gesture gesture, const diagnostics::Snapshot& diagnostic,
                               turntable::RecordSpeed selected_speed);
    Intent run_selected_test(turntable::RecordSpeed selected_speed);
    Intent global_stop(const turntable::Snapshot& state);
    void next_settings_item();

    InteractionConfig config_{};
    Mode mode_ = Mode::Primary;
    SettingsItem settings_item_ = SettingsItem::SystemStatus;
    DiagPage diag_page_ = DiagPage::TargetBrowser;
    DiagTarget diag_target_ = DiagTarget::Platter;
    uint8_t diag_test_ = 0;
};

}  // namespace hmi
