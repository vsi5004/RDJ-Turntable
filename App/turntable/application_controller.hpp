#pragma once

#include "controller.hpp"
#include "fixed_queue.hpp"
#include "diagnostics/diagnostic_controller.hpp"

namespace turntable {

enum class ControlAuthority : uint8_t { Normal, Diagnostic };
enum class ApplicationState : uint8_t { Normal, EnteringDiagnostic, Diagnostic, ExitingDiagnostic };

struct ApplicationSnapshot {
    ControlAuthority authority = ControlAuthority::Normal;
    ApplicationState state = ApplicationState::Normal;
    Snapshot turntable{};
    diagnostics::Snapshot diagnostic{};
};

class ApplicationController {
public:
    ApplicationController(Controller& normal, diagnostics::Controller& diagnostic)
        : normal_(normal), diagnostic_(diagnostic) {}

    bool request_diagnostic_entry();
    void boot_diagnostics();
    void request_diagnostic_exit();
    bool submit_diagnostic(diagnostics::Command command);
    void abort_diagnostic();
    void acknowledge_diagnostic_fault();
    void handle(const Event& event);
    bool post(const Event& event) { return events_.push(event); }
    void tick();

    ControlAuthority authority() const { return authority_; }
    ApplicationState state() const { return state_; }
    ApplicationSnapshot snapshot() const;

private:
    Controller& normal_;
    diagnostics::Controller& diagnostic_;
    ControlAuthority authority_ = ControlAuthority::Normal;
    ApplicationState state_ = ApplicationState::Normal;
    uint32_t entry_operation_id_ = 0;
    FixedQueue<Event, 16> events_{};
};

}  // namespace turntable
