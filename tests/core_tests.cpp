// SPDX-License-Identifier: Apache-2.0
#include <rse/core/machine.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>
using Trace = std::vector<std::pair<rse::Tick, int>>;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Action>
void expect_throw(Action action) {
    try { action(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}
struct Counter final : rse::Component {
    Trace& trace;
    int id;
    rse::Tick now{};
    bool fail_advance{}, fail_reset{};
    int* destroyed{};
    Counter(Trace& events, int identity) : trace(events), id(identity) {}
    ~Counter() override { if (destroyed) ++*destroyed; }
    void reset() override {
        trace.emplace_back(0, id);
        now = 0;
        if (fail_reset) throw std::runtime_error("reset failure");
    }
    void advance_to(rse::Tick t) override {
        check(t == now + 1, "skipped or repeated component tick");
        now = t;
        trace.emplace_back(t, id);
        if (fail_advance) throw std::runtime_error("advance failure");
    }
};
Counter* add(rse::Machine& machine, Trace& trace, int id) {
    auto component = std::make_unique<Counter>(trace, id);
    auto* observer = component.get();
    machine.add(std::move(component));
    return observer;
}
void lifecycle() {
    Trace trace;
    rse::Machine machine;
    expect_throw<std::invalid_argument>([&] { machine.add(nullptr); });
    auto* first = add(machine, trace, 1);
    auto* second = add(machine, trace, 2);
    check(trace == Trace{{0, 1}, {0, 2}}, "registration reset order");
    trace.clear();
    machine.advance_to(0);
    check(trace.empty(), "zero advance dispatched");
    machine.advance_to(2);
    check(trace == Trace{{1, 1}, {1, 2}, {2, 1}, {2, 2}}, "advance order");
    check(machine.tick() == 2 && first->now == 2 && second->now == 2, "committed time");
    trace.clear();
    machine.advance_to(2);
    expect_throw<std::invalid_argument>([&] { machine.advance_to(1); });
    expect_throw<std::logic_error>([&] { add(machine, trace, 3); });
    check(trace.empty() && machine.tick() == 2, "rejected operation mutated state");
    machine.reset();
    check(trace == Trace{{0, 1}, {0, 2}}, "reset order");
    check(machine.tick() == 0 && first->now == 0 && second->now == 0, "reset time");
    expect_throw<std::logic_error>([&] { add(machine, trace, 3); });
    machine.advance_to(1);
    rse::Machine empty;
    empty.advance_to(3);
    check(empty.tick() == 3, "empty advance");
    empty.reset();
    check(empty.tick() == 0, "empty reset");
}
void failures() {
    Trace trace;
    rse::Machine machine;
    auto* first = add(machine, trace, 1);
    auto* second = add(machine, trace, 2);
    auto* third = add(machine, trace, 3);
    machine.advance_to(1);
    trace.clear();
    second->fail_advance = true;
    expect_throw<std::runtime_error>([&] { machine.advance_to(3); });
    check(trace == Trace{{2, 1}, {2, 2}}, "failure dispatch did not stop");
    check(machine.tick() == 1 && first->now == 2 && second->now == 2 && third->now == 1,
          "failure committed time or hid partial state");
    expect_throw<std::logic_error>([&] { machine.advance_to(1); });
    expect_throw<std::logic_error>([&] { add(machine, trace, 4); });
    trace.clear();
    second->fail_reset = true;
    expect_throw<std::runtime_error>([&] { machine.reset(); });
    check(trace == Trace{{0, 1}, {0, 2}} && machine.tick() == 1, "failed reset state");
    expect_throw<std::logic_error>([&] { machine.advance_to(2); });
    second->fail_reset = false;
    second->fail_advance = false;
    machine.reset();
    machine.advance_to(2);
    check(machine.tick() == 2 && third->now == 2, "reset recovery");
    int destroyed = 0;
    {
        rse::Machine registration;
        auto rejected = std::make_unique<Counter>(trace, 5);
        rejected->fail_reset = true;
        rejected->destroyed = &destroyed;
        expect_throw<std::runtime_error>([&] { registration.add(std::move(rejected)); });
        check(destroyed == 1 && registration.tick() == 0, "failed registration state");
        add(registration, trace, 6)->destroyed = &destroyed;
        registration.advance_to(1);
    }
    check(destroyed == 2, "component ownership/destruction");
}
Trace run(const std::vector<rse::Tick>& targets) {
    Trace trace;
    rse::Machine machine;
    add(machine, trace, 1);
    add(machine, trace, 2);
    for (auto target : targets) machine.advance_to(target);
    check(machine.tick() == 8, "final tick");
    return trace;
}
void determinism() {
    Trace expected{{0, 1}, {0, 2}};
    for (rse::Tick tick = 1; tick <= 8; ++tick) {
        expected.emplace_back(tick, 1);
        expected.emplace_back(tick, 2);
    }
    check(run({8}) == expected, "reference trace mismatch");
    check(run({8}) == expected, "repeat mismatch");
    check(run({0, 1, 1, 3, 5, 8}) == expected, "chunking mismatch");
    Trace trace;
    rse::Machine machine;
    add(machine, trace, 1);
    machine.advance_to(8);
    const Trace original = trace;
    trace.clear();
    machine.reset();
    machine.advance_to(8);
    check(trace == original, "reset replay mismatch");
}

// A writer consumes fixed timestamped inputs on a period/phase clock. A reader
// observes the shared state on a different clock, after the writer each tick.
struct Clocked final : rse::Component {
    int& shared;
    Trace& output;
    bool writer;
    Clocked(int& value, Trace& trace, bool writes)
        : shared(value), output(trace), writer(writes) {}
    void reset() override {
        if (writer) shared = 0;
    }
    void advance_to(rse::Tick tick) override {
        if (writer && tick % 2 == 1) {
            if (tick == 1) shared = 4;
            if (tick == 5) shared = 9;
            if (tick == 7) shared = 2;
        }
        if (!writer && tick % 3 == 1) output.emplace_back(tick, shared);
    }
};

Trace clocked_run(const std::vector<rse::Tick>& targets) {
    int shared = 0;
    Trace output;
    rse::Machine machine;
    machine.add(std::make_unique<Clocked>(shared, output, true));
    machine.add(std::make_unique<Clocked>(shared, output, false));
    for (auto target : targets) machine.advance_to(target);
    const Trace first = output;
    output.clear();
    machine.reset();
    for (auto target : targets) machine.advance_to(target);
    check(output == first, "clocked input replay mismatch");
    return output;
}

void clock_domains_and_inputs() {
    const Trace expected{{1, 4}, {4, 4}, {7, 2}, {10, 2}};
    check(clocked_run({10}) == expected, "clock phase or simultaneous visibility mismatch");
    check(clocked_run({10}) == expected, "clocked input repeat mismatch");
    check(clocked_run({0, 1, 4, 5, 7, 7, 10}) == expected,
          "chunking changes clocked inputs or observations");
}
int main() {
    try {
        lifecycle();
        failures();
        determinism();
        clock_domains_and_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
