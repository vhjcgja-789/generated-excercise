#pragma once

#include <utility>
#include <variant>

namespace util {

struct Unit {};

template <typename T, typename E>
class Result {
public:
    static Result Ok(T value) {
        return Result(std::variant<T, E>(std::in_place_index<0>, std::move(value)));
    }

    static Result Fail(E error) {
        return Result(std::variant<T, E>(std::in_place_index<1>, std::move(error)));
    }

    // 状态查询
    bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    T& value() { return std::get<0>(storage_); }
    const T& value() const { return std::get<0>(storage_); }

    E& error() { return std::get<1>(storage_); }
    const E& error() const { return std::get<1>(storage_); }

private:
    explicit Result(std::variant<T, E> storage) : storage_(std::move(storage)) {}

    std::variant<T, E> storage_;
};

} // namespace util
