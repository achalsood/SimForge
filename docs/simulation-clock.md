# Fixed-step simulation timing

`simforge::simulation::SimulationClock` schedules fixed updates independently of
the rendering frame clock. The default is 60 Hz; construction accepts integer
rates from 1 to 1,000,000,000 Hz. Rate is immutable so elapsed simulation time
always equals `tick_count() / ticks_per_second()`. The seconds accessor is a
floating-point presentation of that ratio, not an accumulated floating-point sum.

Call `advance(std::chrono::nanoseconds)` with nonnegative elapsed time and execute
the returned number of simulation updates, each with `fixed_step_seconds()`.
The scheduler uses integer arithmetic for the fractional tick remainder; one
second at 60 Hz produces exactly 60 ticks without rounding the step duration.
It never drops elapsed time or caps catch-up ticks. Callers must consider the
cost of processing a large returned batch. The sandbox calls MovementSystem once
per tick; see [World and ECS](world-ecs.md) for integration examples.

Pause ignores incoming time and preserves the fractional remainder. Resume
continues from that remainder without catching up time supplied while paused.
`single_step()` requires pause and executes one tick regardless of scale, leaving
the remainder intact. `advance_ticks(count)` executes an explicit tick batch
independently of pause, scale, or wall time, for headless/replay drivers.

Scale must be finite and nonnegative. Zero stops automatic ticks. Scaling does
not change the fixed step. At scale 1 input nanoseconds remain exact; other scales
multiply input by a double and round to the nearest nanosecond (ties up) on each
call. Splitting scaled input into different batches can therefore change results.
Invalid inputs and overflow throw before modifying timing state. `reset()` clears
ticks, remainder and pause while retaining rate and scale. Instances require
external synchronization if shared between threads.

The engine exposes its simulation clock and provides initialized-state-checked
`advance_simulation` and `advance_simulation_ticks` methods. `begin_frame()` still
measures real frame time and does not advance simulation. Engine initialization
resets both clocks. Core and simulation have no SDL dependency; window code lives
in `simforge_platform`. The sandbox links that platform library, but its
`--headless` path creates no window and initializes neither SDL nor Vulkan. It
executes 600 explicit ticks by default, updates a movement world, and reports 10
simulated seconds. Entity and tick counts can be selected using `--entities` and
`--ticks`; setup and update timings are reported separately. The current build
still requires the SDL package to build the windowed sandbox.

## Determinism guarantee

Identical ordered inputs, configuration, and pause/step/scale operations in the
same execution environment produce identical tick counts and simulated elapsed
time. Explicit integer tick batches avoid wall-clock and scaling variability.
Unit-scale integer nanosecond scheduling is independent of frame partitioning
when the total input and counters fit their ranges. Real wall-clock frame inputs
are not repeatable; record/replay inputs or drive explicit ticks for repeatability.

Fixed-step timing alone does **not** guarantee deterministic simulation results
across hardware, compilers, or platforms. Floating-point calculations (including
non-unit scaling), system update order, random seeds, concurrency, and external
inputs need their own determinism policies. No such simulation-state guarantee
is claimed here.
