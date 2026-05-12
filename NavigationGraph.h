#pragma once
#include "CyclicEnum.h"
#include <unordered_map>
#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════════════
// NavigationGraph.h
//
// Declares directional (arrow-key) navigation between focus states as data.
// Tab / Shift-Tab are handled automatically by cycle_next / cycle_prev and do
// NOT need to be registered here.
//
// Usage:
//   NavigationGraph<Focus> nav;
//   nav.link(Direction::UP,    Focus::REPLACE,      Focus::FIND);
//   nav.link(Direction::DOWN,  Focus::FIND,         Focus::REPLACE);
//   nav.link(Direction::LEFT,  Focus::BTN_CANCEL,   Focus::BTN_REPLACE_ALL);
//   nav.link(Direction::RIGHT, Focus::BTN_REPLACE,  Focus::BTN_REPLACE_ALL);
//
//   // In the key handler:
//   focus = nav.move(focus, Direction::LEFT);   // no-op if no edge defined
// ═══════════════════════════════════════════════════════════════════════════════

enum class Direction : uint8_t { UP, DOWN, LEFT, RIGHT };

template<CyclicEnum E>
class NavigationGraph {
public:
    // Register a directional edge: from → to when direction is pressed.
    NavigationGraph& link(Direction dir, E from, E to) {
        edges_[key(dir, from)] = to;
        return *this;   // fluent: nav.link(...).link(...).link(...)
    }

    // Return the destination focus, or `current` if no edge is defined.
    [[nodiscard]] E move(E current, Direction dir) const noexcept {
        auto it = edges_.find(key(dir, current));
        return (it != edges_.end()) ? it->second : current;
    }

private:
    // Pack (Direction, E) into a single uint32 key for the map.
    static constexpr uint32_t key(Direction dir, E e) noexcept {
        return (static_cast<uint32_t>(dir) << 16) |
               static_cast<uint32_t>(static_cast<int>(e));
    }

    std::unordered_map<uint32_t, E> edges_;
};
