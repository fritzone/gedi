#ifndef CYCLICENUM_H
#define CYCLICENUM_H

template<typename E>
concept CyclicEnum = std::is_enum_v<E> && requires {
    { E::_count } -> std::convertible_to<E>;
};

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

#define DeclareCyclicEnum(Name, ...)              \
enum class Name : int { __VA_ARGS__, _count };      \
    Name

#endif // CYCLICENUM_H
