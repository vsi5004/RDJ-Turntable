# Turntable host simulator

`turntable_sim` runs the production turntable controller against deterministic virtual hardware.
Virtual time advances without sleeping, so long motions and timeout paths execute in milliseconds on
the host while retaining their intended embedded timing.

The virtual plant models:

- platter acceleration, deceleration, speed lock, and stalls;
- tonearm carriage homing, the outer breakbeam reference, park offset, lead-in travel, tracking, and
  stalls;
- timed tonearm lift movement and stalls;
- operation-ID-tagged completion events, controller deadlines, and injected faults.

Run a checked-in scenario from the repository root:

```powershell
docker run --rm --mount type=bind,source="${PWD}",target=/src rdj-turntable-tests `
  bash /src/tests/run-simulator.sh nominal
```

The runner accepts `nominal`, `recoverable-fault`, `homing-timeout`, or an absolute scenario path
inside the container.

## Scenario commands

One command is accepted per line. Blank lines and text following `#` are ignored.

| Command | Meaning |
|---|---|
| `initialize` | Start the user-requested lift, home, and park sequence. |
| `cancel` | Cancel initialization where permitted. |
| `play`, `pause`, `resume`, `stop` | Submit transport input. |
| `speed 33`, `speed 45` | Select platter speed. |
| `end-side` | Inject end-of-side detection. |
| `wait <ms>` | Advance virtual time by a fixed duration. |
| `await <State> <timeout-ms>` | Advance until a state is reached, failing on timeout. |
| `expect <State>` | Assert the current state without advancing time. |
| `stall platter|carriage|lift on|off` | Freeze or release a virtual mechanism. |
| `fault <code> <policy> <home>` | Inject a typed subsystem fault. |
| `clear-fault`, `ack-fault` | Exercise the two-step fault recovery handshake. |
| `status` | Print current state, speed, carriage position, and home confidence. |

Supported injected fault codes are `platter-driver`, `platter-encoder`, and `carriage-stall`.
Policies are `retryable`, `rehome`, and `power-cycle`; home handling is `keep-home` or `lose-home`.

State names use their C++ spelling, such as `NeedsHome`, `Idle`, `Playing`, and `Fault`. An unknown
command, failed expectation, or expired `await` makes the simulator exit unsuccessfully.
