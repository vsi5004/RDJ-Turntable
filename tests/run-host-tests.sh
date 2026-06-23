#!/usr/bin/env bash
set -eu

cmake -S /src/tests -B /tmp/rdj-host-tests -G Ninja
cmake --build /tmp/rdj-host-tests
ctest --test-dir /tmp/rdj-host-tests --output-on-failure
