// Minimal expected<T, E> for C++20 (std::expected arrives in C++23).
#pragma once

#include <cassert>
#include <utility>
#include <variant>

namespace maritime {

template <class E>
struct Unexpected {
    E error;
};

template <class E>
Unexpected(E) -> Unexpected<E>;

template <class T, class E>
class Expected {
public:
    Expected(T value) : storage_(std::in_place_index<0>, std::move(value)) {}  // NOLINT(google-explicit-constructor)
    Expected(Unexpected<E> err) : storage_(std::in_place_index<1>, std::move(err.error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & {
        assert(has_value());
        return std::get<0>(storage_);
    }
    [[nodiscard]] const T& value() const& {
        assert(has_value());
        return std::get<0>(storage_);
    }
    [[nodiscard]] T&& value() && {
        assert(has_value());
        return std::get<0>(std::move(storage_));
    }
    [[nodiscard]] const E& error() const& {
        assert(!has_value());
        return std::get<1>(storage_);
    }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::variant<T, E> storage_;
};

}  // namespace maritime
