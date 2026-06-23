cmake_minimum_required(VERSION 3.22)
project(RDJTurntableHostTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(turntable_tests
    test_main.cpp
    ../App/turntable/controller.cpp
    ../App/turntable/application_controller.cpp
    ../App/diagnostics/diagnostic_controller.cpp
    ../App/control/platter_feedback.cpp
    ../App/hmi/interaction_controller.cpp
    ../App/hmi/presenter.cpp
    ../App/hmi/screenkey_demo.cpp
)

add_executable(turntable_sim
    ../simulator/main.cpp
    ../simulator/simulation.cpp
    ../App/turntable/controller.cpp
    ../App/hmi/presenter.cpp
)

target_include_directories(turntable_tests PRIVATE ../App)
target_include_directories(turntable_sim PRIVATE ../App ../simulator)

if(MSVC)
    target_compile_options(turntable_tests PRIVATE /W4 /WX)
    target_compile_options(turntable_sim PRIVATE /W4 /WX)
else()
    target_compile_options(turntable_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(turntable_sim PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME turntable_tests COMMAND turntable_tests)
add_test(NAME simulator_nominal
    COMMAND turntable_sim ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/scenarios/nominal.sim)
add_test(NAME simulator_recoverable_fault
    COMMAND turntable_sim ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/scenarios/recoverable-fault.sim)
add_test(NAME simulator_homing_timeout
    COMMAND turntable_sim ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/scenarios/homing-timeout.sim)
