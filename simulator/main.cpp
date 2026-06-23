#include "simulation.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace {

void print_usage(const char* executable)
{
    std::cout << "Usage: " << executable << " <scenario.sim | ->\n"
              << "Use '-' to read a scenario from standard input.\n";
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2 || std::string(argv[1]) == "--help") {
        print_usage(argv[0]);
        return argc == 2 ? 0 : 2;
    }

    simulator::Simulation simulation(std::cout);
    const std::string path = argv[1];
    if (path == "-") return simulation.run_script(std::cin, "stdin") ? 0 : 1;

    std::ifstream input(path);
    if (!input) {
        std::cerr << "Unable to open scenario: " << path << '\n';
        return 2;
    }
    return simulation.run_script(input, path) ? 0 : 1;
}
