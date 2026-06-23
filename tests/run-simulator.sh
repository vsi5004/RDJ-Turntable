#!/usr/bin/env bash
set -eu

scenario="${1:-nominal}"
case "$scenario" in
    nominal|recoverable-fault|homing-timeout)
        scenario="/src/simulator/scenarios/${scenario}.sim"
        ;;
esac

cmake -S /src/tests -B /tmp/rdj-simulator -G Ninja
cmake --build /tmp/rdj-simulator --target turntable_sim
exec /tmp/rdj-simulator/turntable_sim "$scenario"
