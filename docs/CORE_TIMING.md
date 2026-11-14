# Core timing and lifecycle

## Contract

`Tick` is unsigned 64-bit simulated time. A system model chooses one master-clock
unit and documents its physical frequency. The generic core has no NES frequency
or wall-clock dependency. Device clocks use integer master-tick periods and phases:
a device acts at phase + n * period. Choose a master unit that represents all
required edges exactly; do not accumulate floating-point time or round each call.
System-specific ratios and phases will be verified when the NES model is added.

Reset establishes time zero without dispatching time-zero events. Advancement
processes (current, target], one master tick at a time. At each tick, components
run in registration order. This order also resolves simultaneous observations:
a later component sees earlier components' changes at that tick. Equal targets
do nothing; rewinds throw `std::invalid_argument` before dispatch. Advancement
never increments past the requested target, including the maximum `Tick` value.
This is a simple deterministic stepping policy, not a hardware accuracy claim.
Work scales with elapsed ticks times registered components; event skipping is
deferred until it can preserve the same observable ordering.

The machine owns registered components. Registration calls the component's reset
before inserting it. Null registration throws `std::invalid_argument`; registration
after the first positive advance throws `std::logic_error`, including after reset.
A failed registration leaves the existing machine unchanged and destroys the
incoming component. Components must not reenter the machine or change registration
during callbacks. Machine operations are single-threaded.

Components must derive behavior from simulated ticks and explicit state/inputs,
not host time. Reset restores reproducible state, including clock phases and input
queues. Advance callbacks receive consecutive timestamps starting at one, so
caller-selected advancement chunks cannot change callback ordering.

## Failures and recovery

An advance callback exception propagates unchanged and stops dispatch. `tick()`
remains the last fully completed tick; earlier components may already have changed
state at the failing tick. There is no rollback. Further advancement and registration
throw `std::logic_error` until reset succeeds.

Reset invokes components in registration order and commits tick zero only after
all callbacks succeed. A reset exception stops dispatch, propagates unchanged,
and leaves the machine faulted with its previous committed tick. Component state
may be partial. Reset can be retried; components must support recovery from partial
execution and partial reset. Successful reset clears the fault but does not reopen
registration after execution has started.

## M1 verification

`tests/core_tests.cpp` uses project-owned synthetic components; no external ROMs
or hardware traces are needed. Expected observations are ordered callback pairs,
component state, committed machine ticks, and exception types. Cases cover
registration/reset, ordering, equal-time and rewind behavior, failure recovery,
ownership, and identical traces across repeated and differently chunked runs.
Checks remain active in Debug and Release. Hardware clock ratios and bus-cycle
conformance belong to later milestones.

The clock-domain fixture uses a period-2, phase-1 input writer followed by a
period-3, phase-1 reader. Inputs at ticks 1, 5, and 7 set shared values 4, 9,
and 2. Expected observations are (1, 4), (4, 4), (7, 2), and (10, 2), including
same-tick writer-to-reader visibility. Both single and chunked advances, repeated
runs, and reset/replay must match this fixed reference. These are synthetic clock
ratios rather than NES timing evidence.
