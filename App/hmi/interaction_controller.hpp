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
    float diagnostic_closed_loop_velocity = 0.0f;
};

struct NavigationSnapshot {
    Mode mode = Mode::Primary;
    SettingsItem settings_item = SettingsItem::SystemStatus;
};

class InteractionController {
public:
    explicit InteractionController(InteractionConfig config = {}) : config_(config) {}

    Intent handle(Key key, Gesture gesture, const turntable::ApplicationSnapshot& application);
    void synchronize(const turntable::ApplicationSnapshot& application);
    NavigationSnapshot snapshot() const { return {mode_, settings_item_}; }

private:
    Intent handle_normal(Key key, Gesture gesture,
                         const turntable::ApplicationSnapshot& application);
    Intent handle_primary(Key key, Gesture gesture,
                          const turntable::ApplicationSnapshot& application);
    Intent handle_diagnostic(Key key, Gesture gesture,
                             const diagnostics::Snapshot& diagnostic);
    Intent global_stop(const turntable::Snapshot& state);
    void next_settings_item();

    InteractionConfig config_{};
    Mode mode_ = Mode::Primary;
    SettingsItem settings_item_ = SettingsItem::SystemStatus;
};

}  // namespace hmi
