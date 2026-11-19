// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace rse::state {
inline void require(bool valid) {
    if(!valid) throw std::invalid_argument("Invalid or incompatible emulator state");
}
class Writer {
public:
    std::vector<std::uint8_t> bytes;
    template<class... T> void operator()(const T&... value) {(field(value),...);}
private:
    void field(bool value) {bytes.push_back(value);}
    template<std::integral T> void field(T value) {
        auto bits=std::bit_cast<std::make_unsigned_t<T>>(value);
        for(unsigned i=0;i<sizeof(T);++i) {
            bytes.push_back(static_cast<std::uint8_t>(bits));
            bits>>=8;
        }
    }
    template<class T,std::size_t N> void field(const std::array<T,N>& values) {
        for(const auto& value:values) field(value);
    }
    template<class T> void field(const std::optional<T>& value) {
        field(value.has_value());
        if(value) field(*value);
    }
};
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes) {}
    template<class... T> void operator()(T&... value) {(field(value),...);}
    [[nodiscard]] std::size_t remaining() const noexcept {return bytes_.size();}
private:
    std::span<const std::uint8_t> bytes_;
    void field(bool& value) {
        std::uint8_t bits;
        field(bits);
        require(bits<=1);
        value=bits!=0;
    }
    template<std::integral T> void field(T& value) {
        require(bytes_.size()>=sizeof(T));
        std::uint64_t bits=0;
        for(unsigned i=0;i<sizeof(T);++i) bits|=std::uint64_t(bytes_[i])<<(8*i);
        value=std::bit_cast<T>(static_cast<std::make_unsigned_t<T>>(bits));
        bytes_=bytes_.subspan(sizeof(T));
    }
    template<class T,std::size_t N> void field(std::array<T,N>& values) {
        for(auto& value:values) field(value);
    }
    template<class T> void field(std::optional<T>& value) {
        bool present;
        field(present);
        if(present) field(value.emplace());
        else value.reset();
    }
};
inline std::uint64_t hash(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t value=14695981039346656037ull;
    for(const auto byte:bytes) value=(value^byte)*1099511628211ull;
    return value;
}
}
