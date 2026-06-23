#pragma once

#include <cstdint>

namespace diagnostics {

enum class State : uint8_t { Inactive, Ready, Running, Stopping, Fault };
enum class StopIntent : uint8_t { AbortCommand, ExitMode };
enum class Target : uint8_t {
    DisplaysAndKeys,
    PlatterEncoder,
    PlatterMotor,
    PlatterFeedback,
    TonearmCarriage,
    TonearmLift,
    Integrated,
};

enum class Action : uint8_t {
    ReadStatus,
    DisplayPattern,
    OpenLoopSpin,
    ElectricalAlign,
    EncoderAutoCal,
    ClosedLoopVelocity,
    Stop,
    Jog,
    Home,
    MoveAbsolute,
    Raise,
    Lower,
};

struct Parameters {
    float value = 0.0f;
    float limit = 0.0f;
    int32_t integer = 0;
};

struct Command {
    Target target = Target::DisplaysAndKeys;
    Action action = Action::ReadStatus;
    Parameters parameters{};
    uint32_t command_id = 0;
};

enum class ExecutionState : uint8_t { Idle, Running, Complete, Failed };

struct ExecutionReport {
    ExecutionState state = ExecutionState::Idle;
    int32_t result_code = 0;
    float primary = 0.0f;
    float secondary = 0.0f;
    bool invalidates_home = false;
};

class IExecutor {
public:
    virtual ~IExecutor() = default;
    virtual bool start(const Command& command) = 0;
    virtual void abort() = 0;
    virtual void safe_stop() = 0;
    virtual ExecutionReport poll() = 0;
};

struct Snapshot {
    State state = State::Inactive;
    Command command{};
    ExecutionReport report{};
    bool exit_ready = false;
};

class Controller {
public:
    explicit Controller(IExecutor& executor) : executor_(executor) {}

    void enter();
    bool submit(Command command);
    void abort();
    void request_exit();
    void acknowledge_fault();
    void tick();

    State state() const { return state_; }
    bool exit_ready() const { return exit_ready_; }
    bool home_invalidated() const { return home_invalidated_; }
    Snapshot snapshot() const;

private:
    IExecutor& executor_;
    State state_ = State::Inactive;
    StopIntent stop_intent_ = StopIntent::AbortCommand;
    Command command_{};
    ExecutionReport report_{};
    uint32_t next_command_id_ = 0;
    bool exit_ready_ = false;
    bool home_invalidated_ = false;
};

}  // namespace diagnostics
