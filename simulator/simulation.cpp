#include "simulation.hpp"

#include "control/platter_feedback.hpp"
#include "hmi/presenter.hpp"
#include "turntable/controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace simulator {
namespace {

using turntable::Event;
using turntable::EventType;
using turntable::FaultCode;
using turntable::FaultRecord;
using turntable::FaultSource;
using turntable::HomeConfidence;
using turntable::LiftPosition;
using turntable::MotionState;
using turntable::PositionConfidence;
using turntable::RecordSpeed;
using turntable::RecoveryPolicy;
using turntable::State;

constexpr uint32_t kStepMs = 20;
constexpr float kParkPositionMm = 3.0f;
constexpr float kLeadInPositionMm = 8.0f;
constexpr uint32_t kAbiCountsPerRevolution = 4000;
constexpr uint32_t kMeasurementTimerHz = 84000000;
constexpr double kPi = 3.14159265358979323846;

float target_rpm(RecordSpeed speed)
{
    return speed == RecordSpeed::Rpm45 ? 45.0f : 33.3333f;
}

float approach(float value, float target, float maximum_delta)
{
    if (value < target) return std::min(value + maximum_delta, target);
    return std::max(value - maximum_delta, target);
}

const char* state_name(State state)
{
    switch (state) {
    case State::NeedsHome: return "NeedsHome";
    case State::RaisingForInitialization: return "RaisingForInitialization";
    case State::HomingCarriage: return "HomingCarriage";
    case State::ParkingCarriage: return "ParkingCarriage";
    case State::Idle: return "Idle";
    case State::SpinningUpForPlay: return "SpinningUpForPlay";
    case State::SeekingLeadIn: return "SeekingLeadIn";
    case State::LoweringForPlay: return "LoweringForPlay";
    case State::Playing: return "Playing";
    case State::RaisingForPause: return "RaisingForPause";
    case State::Paused: return "Paused";
    case State::LoweringForResume: return "LoweringForResume";
    case State::RaisingForSpeedChange: return "RaisingForSpeedChange";
    case State::ChangingSpeedForPlayback: return "ChangingSpeedForPlayback";
    case State::LoweringAfterSpeedChange: return "LoweringAfterSpeedChange";
    case State::ChangingSpeedWhilePaused: return "ChangingSpeedWhilePaused";
    case State::RaisingForStop: return "RaisingForStop";
    case State::ReturningHomeWithPlatter: return "ReturningHomeWithPlatter";
    case State::StoppingPlatter: return "StoppingPlatter";
    case State::Interrupted: return "Interrupted";
    case State::SpinningUpForResume: return "SpinningUpForResume";
    case State::ReturningHomeWithoutPlatter: return "ReturningHomeWithoutPlatter";
    case State::Maintenance: return "Maintenance";
    case State::Fault: return "Fault";
    }
    return "Unknown";
}

const char* event_name(EventType type)
{
    switch (type) {
    case EventType::LiftSettled: return "lift-settled";
    case EventType::HomeReferenceFound: return "home-reference-found";
    case EventType::CarriageAtPark: return "carriage-at-park";
    case EventType::CarriageAtLeadIn: return "carriage-at-lead-in";
    case EventType::PlatterSpeedLocked: return "platter-speed-locked";
    case EventType::PlatterStopped: return "platter-stopped";
    default: return "event";
    }
}

std::optional<State> parse_state(const std::string& text)
{
    static const std::unordered_map<std::string, State> states = {
        {"NeedsHome", State::NeedsHome},
        {"RaisingForInitialization", State::RaisingForInitialization},
        {"HomingCarriage", State::HomingCarriage},
        {"ParkingCarriage", State::ParkingCarriage},
        {"Idle", State::Idle},
        {"SpinningUpForPlay", State::SpinningUpForPlay},
        {"SeekingLeadIn", State::SeekingLeadIn},
        {"LoweringForPlay", State::LoweringForPlay},
        {"Playing", State::Playing},
        {"RaisingForPause", State::RaisingForPause},
        {"Paused", State::Paused},
        {"LoweringForResume", State::LoweringForResume},
        {"RaisingForSpeedChange", State::RaisingForSpeedChange},
        {"ChangingSpeedForPlayback", State::ChangingSpeedForPlayback},
        {"LoweringAfterSpeedChange", State::LoweringAfterSpeedChange},
        {"ChangingSpeedWhilePaused", State::ChangingSpeedWhilePaused},
        {"RaisingForStop", State::RaisingForStop},
        {"ReturningHomeWithPlatter", State::ReturningHomeWithPlatter},
        {"StoppingPlatter", State::StoppingPlatter},
        {"Interrupted", State::Interrupted},
        {"SpinningUpForResume", State::SpinningUpForResume},
        {"ReturningHomeWithoutPlatter", State::ReturningHomeWithoutPlatter},
        {"Maintenance", State::Maintenance},
        {"Fault", State::Fault},
    };
    const auto found = states.find(text);
    if (found == states.end()) return std::nullopt;
    return found->second;
}

std::optional<uint32_t> parse_u32(const std::string& text)
{
    try {
        std::size_t consumed = 0;
        const unsigned long value = std::stoul(text, &consumed);
        if (consumed != text.size() || value > 0xffffffffUL) return std::nullopt;
        return static_cast<uint32_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> words_from(std::string line)
{
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos) line.erase(comment);
    std::istringstream stream(line);
    std::vector<std::string> words;
    for (std::string word; stream >> word;) words.push_back(std::move(word));
    return words;
}

class SimClock final : public turntable::IClock {
public:
    uint32_t now_ms() const override { return now_ms_; }
    void advance(uint32_t duration_ms) { now_ms_ += duration_ms; }

private:
    uint32_t now_ms_ = 0;
};

class SimPlatter final : public turntable::IPlatter {
public:
    SimPlatter()
        : estimator_(platter_feedback::AbiEstimatorConfig{
              kAbiCountsPerRevolution, kMeasurementTimerHz, 32, kMeasurementTimerHz / 10u,
              0.12f}),
          lock_(platter_feedback::LockConfig{0.05f, 500})
    {
    }

    void start(RecordSpeed speed, uint32_t operation_id) override
    {
        target_rpm_ = target_rpm(speed);
        operation_id_ = operation_id;
        stopping_ = false;
        completion_reported_ = false;
        lock_.set_target(target_rpm_);
        trace_.reset();
    }

    void stop(uint32_t operation_id) override
    {
        target_rpm_ = 0.0f;
        operation_id_ = operation_id;
        stopping_ = true;
        completion_reported_ = false;
        lock_.reset();
    }

    void emergency_stop() override
    {
        target_rpm_ = 0.0f;
        operation_id_ = 0;
        stopping_ = false;
        completion_reported_ = true;
        lock_.reset();
        estimator_.reset();
        trace_.reset();
    }

    std::optional<Event> update(uint32_t now_ms, uint32_t duration_ms)
    {
        if (!stalled_) {
            const float rate = target_rpm_ > shaft_rpm_ ? 15.0f : 25.0f;
            shaft_rpm_ = approach(shaft_rpm_, target_rpm_,
                                  rate * static_cast<float>(duration_ms) / 1000.0f);
        }

        update_encoder(now_ms, duration_ms);
        const bool speed_locked = lock_.update(now_ms, measured_rpm(), estimator_.valid());
        trace_.update(now_ms, target_rpm_, measured_rpm(), speed_locked);
        if (target_rpm_ > 0.0f && speed_locked && !completion_reported_) {
            completion_reported_ = true;
            return Event::completion(EventType::PlatterSpeedLocked, operation_id_);
        }

        if (stopping_ && shaft_rpm_ <= 0.01f && !completion_reported_) {
            shaft_rpm_ = 0.0f;
            estimator_.reset();
            lock_.reset();
            trace_.reset();
            completion_reported_ = true;
            return Event::completion(EventType::PlatterStopped, operation_id_);
        }
        return std::nullopt;
    }

    void set_stalled(bool stalled) { stalled_ = stalled; }
    float rpm() const { return measured_rpm(); }
    float physical_rpm() const { return shaft_rpm_; }
    bool measurement_valid() const { return estimator_.valid(); }
    bool locked() const { return lock_.locked(); }
    const platter_feedback::SpeedTrace& speed_trace() const { return trace_; }

private:
    float measured_rpm() const { return estimator_.valid() ? estimator_.rpm() : 0.0f; }

    void update_encoder(uint32_t now_ms, uint32_t duration_ms)
    {
        const uint32_t start_ms = now_ms - duration_ms;
        const uint64_t start_ticks = static_cast<uint64_t>(start_ms) * kMeasurementTimerHz / 1000u;
        const uint64_t duration_ticks = static_cast<uint64_t>(duration_ms)
            * kMeasurementTimerHz / 1000u;

        /* A once-per-revolution term approximates encoder eccentricity/wow; the second term gives
         * the future sparkline something less mathematically pristine to display. */
        const double time_seconds = static_cast<double>(now_ms) / 1000.0;
        const double encoder_error = 0.0007 * std::sin(2.0 * kPi * revolutions_)
            + 0.00025 * std::sin(2.0 * kPi * time_seconds / 0.73);
        const double observed_rpm = static_cast<double>(shaft_rpm_) * (1.0 + encoder_error);
        const double count_delta = observed_rpm * static_cast<double>(kAbiCountsPerRevolution)
            * static_cast<double>(duration_ms) / 60000.0;
        const double start_count = encoder_count_;
        const double end_count = start_count + count_delta;

        while (static_cast<double>(next_edge_count_) <= end_count && count_delta > 0.0) {
            const double fraction = (static_cast<double>(next_edge_count_) - start_count)
                / count_delta;
            const uint64_t edge_ticks = start_ticks
                + static_cast<uint64_t>(fraction * static_cast<double>(duration_ticks) + 0.5);
            platter_feedback::AbiTimerSample sample;
            sample.position_counts = static_cast<uint32_t>(next_edge_count_);
            sample.timestamp_ticks = static_cast<uint32_t>(edge_ticks);
            if (next_edge_count_ % kAbiCountsPerRevolution == 0) {
                ++index_sequence_;
                index_timestamp_ticks_ = sample.timestamp_ticks;
            }
            sample.index_sequence = index_sequence_;
            sample.index_timestamp_ticks = index_timestamp_ticks_;
            estimator_.on_sample(sample);
            ++next_edge_count_;
        }

        encoder_count_ = end_count;
        revolutions_ += count_delta / static_cast<double>(kAbiCountsPerRevolution);
        const uint64_t now_ticks = static_cast<uint64_t>(now_ms) * kMeasurementTimerHz / 1000u;
        estimator_.update(static_cast<uint32_t>(now_ticks));
    }

    float shaft_rpm_ = 0.0f;
    float target_rpm_ = 0.0f;
    double encoder_count_ = 0.0;
    double revolutions_ = 0.0;
    uint64_t next_edge_count_ = 1;
    uint32_t index_sequence_ = 0;
    uint32_t index_timestamp_ticks_ = 0;
    uint32_t operation_id_ = 0;
    bool stopping_ = false;
    bool stalled_ = false;
    bool completion_reported_ = false;
    platter_feedback::AbiRpmEstimator estimator_;
    platter_feedback::SpeedLockDetector lock_;
    platter_feedback::SpeedTrace trace_;
};

class SimCarriage final : public turntable::ITonearmCarriage {
public:
    void home(uint32_t operation_id) override
    {
        motion_ = Motion::Homing;
        target_mm_ = 0.0f;
        operation_id_ = operation_id;
    }

    void establish_home_reference() override { position_mm_ = 0.0f; }

    void move_to_park(uint32_t operation_id) override
    {
        motion_ = Motion::Parking;
        target_mm_ = kParkPositionMm;
        operation_id_ = operation_id;
    }

    void move_to_lead_in(uint32_t operation_id) override
    {
        motion_ = Motion::Seeking;
        target_mm_ = kLeadInPositionMm;
        operation_id_ = operation_id;
    }

    void stop() override { motion_ = Motion::Idle; }
    void set_tracking(bool enabled) override { tracking_ = enabled; }
    void mark_home_valid() override { home_ = HomeConfidence::Valid; }
    void invalidate_home() override { home_ = HomeConfidence::Unknown; }
    HomeConfidence home_confidence() const override { return home_; }
    bool at_park() const override { return std::fabs(position_mm_ - kParkPositionMm) < 0.01f; }

    std::optional<Event> update(uint32_t duration_ms)
    {
        if (tracking_ && motion_ == Motion::Idle && !stalled_)
            position_mm_ += 0.02f * static_cast<float>(duration_ms) / 1000.0f;

        if (motion_ == Motion::Idle || stalled_) return std::nullopt;
        const float speed = motion_ == Motion::Homing ? 5.0f : 10.0f;
        position_mm_ = approach(position_mm_, target_mm_,
                                speed * static_cast<float>(duration_ms) / 1000.0f);
        if (std::fabs(position_mm_ - target_mm_) > 0.001f) return std::nullopt;

        position_mm_ = target_mm_;
        const Motion completed = motion_;
        motion_ = Motion::Idle;
        if (completed == Motion::Homing)
            return Event::completion(EventType::HomeReferenceFound, operation_id_);
        if (completed == Motion::Parking)
            return Event::completion(EventType::CarriageAtPark, operation_id_);
        return Event::completion(EventType::CarriageAtLeadIn, operation_id_);
    }

    void set_stalled(bool stalled) { stalled_ = stalled; }
    float position_mm() const { return position_mm_; }
    MotionState motion_state() const
    {
        return motion_ == Motion::Idle ? MotionState::Idle : MotionState::Moving;
    }

private:
    enum class Motion { Idle, Homing, Parking, Seeking };

    float position_mm_ = 20.0f;
    float target_mm_ = 20.0f;
    uint32_t operation_id_ = 0;
    HomeConfidence home_ = HomeConfidence::Unknown;
    Motion motion_ = Motion::Idle;
    bool tracking_ = false;
    bool stalled_ = false;
};

class SimLift final : public turntable::ITonearmLift {
public:
    void raise(uint32_t operation_id) override
    {
        begin(LiftPosition::Raised, operation_id);
    }

    void lower(uint32_t operation_id) override
    {
        begin(LiftPosition::Lowered, operation_id);
    }

    std::optional<Event> update(uint32_t duration_ms)
    {
        if (!moving_ || stalled_) return std::nullopt;
        elapsed_ms_ += duration_ms;
        if (elapsed_ms_ < 600) return std::nullopt;
        moving_ = false;
        position_ = target_;
        return Event::lift_settled(operation_id_, position_, PositionConfidence::Estimated);
    }

    void set_stalled(bool stalled) { stalled_ = stalled; }
    LiftPosition position() const { return position_; }
    MotionState motion_state() const { return moving_ ? MotionState::Moving : MotionState::Idle; }

private:
    void begin(LiftPosition target, uint32_t operation_id)
    {
        target_ = target;
        operation_id_ = operation_id;
        elapsed_ms_ = 0;
        moving_ = true;
    }

    LiftPosition position_ = LiftPosition::Unknown;
    LiftPosition target_ = LiftPosition::Unknown;
    uint32_t operation_id_ = 0;
    uint32_t elapsed_ms_ = 0;
    bool moving_ = false;
    bool stalled_ = false;
};

}  // namespace

class Simulation::Impl {
public:
    explicit Impl(std::ostream& output)
        : output_(output), controller_(clock_, platter_, carriage_, lift_)
    {
        output_ << "[       0 ms] STATE " << state_name(controller_.state()) << '\n';
    }

    bool run_script(std::istream& input, std::string_view label)
    {
        output_ << "--- scenario: " << label << " ---\n";
        std::string line;
        uint32_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            const std::vector<std::string> words = words_from(line);
            if (words.empty()) continue;
            if (!execute(words)) {
                output_ << "ERROR " << label << ':' << line_number << ": " << line << '\n';
                return false;
            }
        }
        output_ << "--- scenario passed at " << clock_.now_ms() << " ms ---\n";
        return true;
    }

private:
    bool execute(const std::vector<std::string>& words)
    {
        const std::string& command = words[0];
        if (command == "initialize" && words.size() == 1)
            return input(Event::simple(EventType::InitializeRequested), command);
        if (command == "cancel" && words.size() == 1)
            return input(Event::simple(EventType::CancelRequested), command);
        if (command == "play" && words.size() == 1)
            return input(Event::simple(EventType::PlayRequested), command);
        if (command == "pause" && words.size() == 1)
            return input(Event::simple(EventType::PauseRequested), command);
        if (command == "resume" && words.size() == 1)
            return input(Event::simple(EventType::ResumeRequested), command);
        if (command == "stop" && words.size() == 1)
            return input(Event::simple(EventType::StopRequested), command);
        if (command == "end-side" && words.size() == 1)
            return input(Event::simple(EventType::EndOfSideDetected), command);
        if (command == "clear-fault" && words.size() == 1)
            return input(Event::simple(EventType::FaultConditionCleared), command);
        if (command == "ack-fault" && words.size() == 1)
            return input(Event::simple(EventType::AcknowledgeFaultRequested), command);

        if (command == "speed" && words.size() == 2) {
            if (words[1] == "33") return input(Event::speed_change(RecordSpeed::Rpm33), "speed 33");
            if (words[1] == "45") return input(Event::speed_change(RecordSpeed::Rpm45), "speed 45");
            return false;
        }

        if (command == "wait" && words.size() == 2) {
            const auto duration = parse_u32(words[1]);
            if (!duration) return false;
            output_ << timestamp() << " WAIT  " << *duration << " ms\n";
            advance(*duration);
            return true;
        }

        if (command == "await" && words.size() == 3) {
            const auto expected = parse_state(words[1]);
            const auto timeout = parse_u32(words[2]);
            if (!expected || !timeout) return false;
            output_ << timestamp() << " AWAIT " << words[1] << " (" << *timeout << " ms)\n";
            return await_state(*expected, *timeout);
        }

        if (command == "expect" && words.size() == 2) {
            const auto expected = parse_state(words[1]);
            if (!expected) return false;
            if (controller_.state() != *expected) {
                output_ << timestamp() << " EXPECT failed: wanted " << words[1] << ", got "
                        << state_name(controller_.state()) << '\n';
                return false;
            }
            output_ << timestamp() << " EXPECT " << words[1] << " passed\n";
            return true;
        }

        if (command == "stall" && words.size() == 3) {
            const bool stalled = words[2] == "on";
            if (!stalled && words[2] != "off") return false;
            if (words[1] == "platter") platter_.set_stalled(stalled);
            else if (words[1] == "carriage") carriage_.set_stalled(stalled);
            else if (words[1] == "lift") lift_.set_stalled(stalled);
            else return false;
            output_ << timestamp() << " STALL " << words[1] << ' ' << words[2] << '\n';
            return true;
        }

        if (command == "fault" && words.size() == 4)
            return inject_fault(words[1], words[2], words[3]);

        if (command == "status" && words.size() == 1) {
            print_status();
            return true;
        }
        if (command == "screenkeys" && words.size() == 1) {
            print_screenkeys();
            return true;
        }
        if (command == "speed-trace" && words.size() == 1) {
            print_speed_trace();
            return true;
        }
        return false;
    }

    bool input(const Event& event, const std::string& description)
    {
        output_ << timestamp() << " INPUT " << description << '\n';
        handle(event);
        return true;
    }

    bool inject_fault(const std::string& code_text, const std::string& recovery_text,
                      const std::string& home_text)
    {
        FaultCode code = FaultCode::Unknown;
        FaultSource source = FaultSource::Product;
        if (code_text == "platter-driver") {
            code = FaultCode::PlatterDriver;
            source = FaultSource::Platter;
        } else if (code_text == "platter-encoder") {
            code = FaultCode::PlatterEncoder;
            source = FaultSource::Platter;
        } else if (code_text == "carriage-stall") {
            code = FaultCode::CarriageStall;
            source = FaultSource::Carriage;
        } else {
            return false;
        }

        RecoveryPolicy recovery = RecoveryPolicy::Retryable;
        if (recovery_text == "rehome") recovery = RecoveryPolicy::RequiresCarriageHome;
        else if (recovery_text == "power-cycle") recovery = RecoveryPolicy::RequiresPowerCycle;
        else if (recovery_text != "retryable") return false;

        const bool invalidate_home = home_text == "lose-home";
        if (!invalidate_home && home_text != "keep-home") return false;
        const FaultRecord fault{code, source, recovery, invalidate_home, clock_.now_ms()};
        output_ << timestamp() << " FAULT " << code_text << ' ' << recovery_text << ' '
                << home_text << '\n';
        handle(Event::detected_fault(fault));
        return true;
    }

    void advance(uint32_t duration_ms)
    {
        uint32_t elapsed = 0;
        while (elapsed < duration_ms) {
            const uint32_t step = std::min(kStepMs, duration_ms - elapsed);
            clock_.advance(step);
            elapsed += step;

            std::vector<Event> events;
            if (auto event = platter_.update(clock_.now_ms(), step)) events.push_back(*event);
            if (auto event = carriage_.update(step)) events.push_back(*event);
            if (auto event = lift_.update(step)) events.push_back(*event);

            update_observations();
            for (const Event& event : events) {
                output_ << timestamp() << " EVENT " << event_name(event.type)
                        << " op=" << event.operation_id << '\n';
                handle(event);
            }
            controller_.tick();
            trace_state_change();
            update_observations();
        }
    }

    bool await_state(State expected, uint32_t timeout_ms)
    {
        if (controller_.state() == expected) return true;
        uint32_t elapsed = 0;
        while (elapsed < timeout_ms) {
            const uint32_t step = std::min(kStepMs, timeout_ms - elapsed);
            advance(step);
            elapsed += step;
            if (controller_.state() == expected) return true;
        }
        output_ << timestamp() << " AWAIT failed: wanted " << state_name(expected)
                << ", got " << state_name(controller_.state()) << '\n';
        return false;
    }

    void handle(const Event& event)
    {
        controller_.handle(event);
        trace_state_change();
        update_observations();
    }

    void trace_state_change()
    {
        if (controller_.state() == last_state_) return;
        output_ << timestamp() << " STATE " << state_name(last_state_) << " -> "
                << state_name(controller_.state()) << '\n';
        last_state_ = controller_.state();
    }

    void update_observations()
    {
        turntable::Observations observations;
        observations.measured_rpm = platter_.rpm();
        observations.speed_locked = platter_.locked();
        observations.carriage_position_mm = carriage_.position_mm();
        observations.lift.position = lift_.position();
        observations.lift.motion = lift_.motion_state();
        observations.lift.confidence = lift_.position() == LiftPosition::Unknown
            ? PositionConfidence::Unknown
            : PositionConfidence::Estimated;
        controller_.update_observations(observations);
    }

    void print_status()
    {
        const turntable::Snapshot snapshot = controller_.snapshot();
        output_ << timestamp() << " STATUS state=" << state_name(snapshot.state)
                << " rpm=" << std::fixed << std::setprecision(2) << snapshot.measured_rpm
                << " physical=" << platter_.physical_rpm()
                << " carriage=" << snapshot.carriage_position_mm << "mm home="
                << (snapshot.home == HomeConfidence::Valid ? "valid" : "unknown")
                << " abi=" << (platter_.measurement_valid() ? "valid" : "stale") << '\n';
    }

    void print_screenkeys()
    {
        turntable::ApplicationSnapshot application;
        application.authority = turntable::ControlAuthority::Normal;
        application.state = turntable::ApplicationState::Normal;
        application.turntable = controller_.snapshot();
        const hmi::View view = hmi::present(application, {}, 0, &platter_.speed_trace());
        for (std::size_t index = 0; index < 3; ++index) {
            const hmi::KeyView& key = view.keys[index];
            output_ << timestamp() << " KEY" << index << ' ' << key.header << " | "
                    << key.action << " | " << key.detail
                    << (key.speed_sparkline.count > 0
                            ? " | RIPPLE " + std::to_string(key.speed_sparkline.count) : "")
                    << (key.enabled ? "" : " [disabled]") << '\n';
        }
    }

    void print_speed_trace()
    {
        const platter_feedback::SpeedTrace& trace = platter_.speed_trace();
        output_ << timestamp() << " SPEED-TRACE samples=" << trace.size()
                << " deviation-millirpm=";
        for (std::size_t index = 0; index < trace.size(); ++index) {
            if (index != 0) output_ << ',';
            output_ << trace.deviation_millirpm(index);
        }
        output_ << '\n';
    }

    std::string timestamp() const
    {
        std::ostringstream stream;
        stream << '[' << std::setw(8) << clock_.now_ms() << " ms]";
        return stream.str();
    }

    std::ostream& output_;
    SimClock clock_;
    SimPlatter platter_;
    SimCarriage carriage_;
    SimLift lift_;
    turntable::Controller controller_;
    State last_state_ = State::NeedsHome;
};

Simulation::Simulation(std::ostream& output) : impl_(std::make_unique<Impl>(output)) {}
Simulation::~Simulation() = default;

bool Simulation::run_script(std::istream& input, std::string_view label)
{
    return impl_->run_script(input, label);
}

}  // namespace simulator
