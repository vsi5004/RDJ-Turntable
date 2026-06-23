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
)

target_include_directories(turntable_tests PRIVATE ../App)

if(MSVC)
    target_compile_options(turntable_tests PRIVATE /W4 /WX)
else()
    target_compile_options(turntable_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME turntable_tests COMMAND turntable_tests)
