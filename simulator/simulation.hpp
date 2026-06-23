#pragma once

#include <iosfwd>
#include <memory>
#include <string_view>

namespace simulator {

class Simulation {
public:
    explicit Simulation(std::ostream& output);
    ~Simulation();

    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    bool run_script(std::istream& input, std::string_view label);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace simulator
