#pragma once
#include <type_traits>

// ═══════════════════════════════════════════════════════════════════════════════
// CyclicEnum.h
//
// Concept + helpers for strongly-typed enums that wrap around.
//
// Usage:
//   DeclareCyclicEnum(Focus, FIND, REPLACE, BTN_OK, BTN_CANCEL);
//   Focus f = Focus::FIND;
//   f = cycle_next(f);   // FIND → REPLACE
//   f = cycle_prev(f);   // REPLACE → FIND
// ═══════════════════════════════════════════════════════════════════════════════

// Note: the return-type constraint { E::_count } -> std::convertible_to<...>
// fails for nested enum classes on GCC < 13. We require only that _count
// exists as an enumerator — which is all we actually need.
template<typename E>
concept CyclicEnum = std::is_enum_v<E> && requires { E::_count; };

template<CyclicEnum E>
[[nodiscard]] constexpr E cycle_next(E e) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>((static_cast<U>(e) + 1) % static_cast<U>(E::_count));
}

template<CyclicEnum E>
[[nodiscard]] constexpr E cycle_prev(E e) noexcept {
    using U = std::underlying_type_t<E>;
    const auto n = static_cast<U>(E::_count);
    return static_cast<E>((static_cast<U>(e) + n - 1) % n);
}

template<CyclicEnum E>
[[nodiscard]] constexpr int cyclic_index(E e) noexcept {
    return static_cast<int>(e);
}

// Declares the enum class and injects the _count sentinel automatically.
//
//   DeclareCyclicEnum(Focus, FIND, REPLACE, BTN_OK);
//   →  enum class Focus : int { FIND, REPLACE, BTN_OK, _count };
//
#define DeclareCyclicEnum(Name, ...)                      \
enum class Name : int { __VA_ARGS__, _count }
