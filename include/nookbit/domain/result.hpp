#pragma once

#include <optional>
#include <string>
#include <utility>

namespace nookbit {

struct Error {
    std::string code;
    std::string message;
};

template <typename T>
class Result final {
public:
    static Result success(T value) {
        return Result(std::move(value), std::nullopt);
    }

    static Result failure(Error error) {
        return Result(std::nullopt, std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept { return value_.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] const T& value() const { return value_.value(); }
    [[nodiscard]] T& value() { return value_.value(); }
    [[nodiscard]] const Error& error() const { return error_.value(); }

private:
    Result(std::optional<T> value, std::optional<Error> error)
        : value_(std::move(value)), error_(std::move(error)) {}

    std::optional<T> value_;
    std::optional<Error> error_;
};

template <>
class Result<void> final {
public:
    static Result success() { return Result(true, std::nullopt); }
    static Result failure(Error error) { return Result(false, std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return succeeded_; }
    [[nodiscard]] explicit operator bool() const noexcept { return succeeded_; }
    [[nodiscard]] const Error& error() const { return error_.value(); }

private:
    Result(bool succeeded, std::optional<Error> error)
        : succeeded_(succeeded), error_(std::move(error)) {}

    bool succeeded_;
    std::optional<Error> error_;
};

} // namespace nookbit
