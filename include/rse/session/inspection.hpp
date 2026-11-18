// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "types.hpp"
#include <vector>

namespace rse::session {
enum class ClockDomain { master, cpu, video };
enum class ValueKind { memory, register_value, signal };
enum class AddressSpace { physical, mapped };
struct ValueInfo {
    std::string_view id;
    ValueKind kind;
    ClockDomain clock;
    unsigned bits;
    // Memory extent in byte addresses. Scalars use address zero and extent one.
    std::uint32_t extent;
    AddressSpace space{AddressSpace::physical};
};
struct EventInfo {
    std::string_view id;
    ClockDomain clock;
    // Empty for events without a bus address.
    std::string_view address_domain;
    unsigned data_bits;
};
enum class Boundary { master_tick, cpu_cycle, instruction, scanline, frame };
enum class Phase { before, after };
struct InspectionInfo {
    std::span<const ValueInfo> values;
    std::span<const EventInfo> events;
    std::span<const Boundary> boundaries;
};
struct ValueRange {
    std::string_view id;
    std::uint32_t first{}, count{1};
};
struct Subscription {
    std::size_t capacity{};
    // Empty matches all events when events is true.
    bool events{};
    std::string_view event_id;
    // Optional filter on the mapped bus address of an event.
    std::optional<ValueRange> addresses;
    std::optional<Boundary> boundary;
    Phase phase{Phase::after};
    // Optional single value to sample at the boundary. Subsequent records
    // carry only changes from the preceding sample.
    std::optional<ValueRange> delta;
};
enum class RecordKind { event, boundary, delta };
struct ObservationRecord {
    std::uint64_t sequence{};
    Tick tick{};
    RecordKind kind{};
    Phase phase{};
    // Stable ID from the schema for events and deltas.
    std::string_view id;
    std::optional<std::uint32_t> address;
    std::optional<std::uint64_t> value, previous;
    // Event-native cycle/dot index, or boundary-native index.
    std::uint64_t index{};
    bool initial{};
    bool operator==(const ObservationRecord&) const = default;
};
struct ObservationBatch {
    std::vector<ObservationRecord> records;
    std::uint64_t lost{};
};

// Serialized with its owning session. See docs/INSPECTION.md for lifetime,
// reset, phase, and overflow semantics.
class Inspection {
public:
    virtual ~Inspection() = default;
    [[nodiscard]] virtual InspectionInfo schema() const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::uint64_t> peek(std::string_view id, std::uint32_t address = 0) const = 0;
    virtual void read(std::string_view id, std::uint32_t first,
                      std::span<std::optional<std::uint64_t>> output) const = 0;
    virtual void subscribe(const Subscription& subscription) = 0;
    virtual void unsubscribe() noexcept = 0;
    [[nodiscard]] virtual ObservationBatch drain() = 0;
};
}
