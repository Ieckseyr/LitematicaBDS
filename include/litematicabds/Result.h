#pragma once

// 轻量 Result<T>：核心库不向调用方抛异常（除 std::bad_alloc），
// 所有可预期失败都走 Result，平台层只负责把 message 转成玩家可读提示。

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace litematicabds {

struct Error {
    std::string message;

    explicit Error(std::string msg) : message(std::move(msg)) {}
    explicit Error(char const* msg) : message(msg) {}
};

template <class T>
class Result {
public:
    Result(T value) : mStorage(std::move(value)) {}
    Result(Error error) : mStorage(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(mStorage); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T const& value() const& {
        if (!ok()) throw std::runtime_error(error().message);
        return std::get<T>(mStorage);
    }
    [[nodiscard]] T& value() & {
        if (!ok()) throw std::runtime_error(error().message);
        return std::get<T>(mStorage);
    }
    [[nodiscard]] T&& take() && {
        if (!ok()) throw std::runtime_error(error().message);
        return std::move(std::get<T>(mStorage));
    }

    [[nodiscard]] Error const& error() const& {
        if (ok()) throw std::runtime_error("Result<T>::error() called on success");
        return std::get<Error>(mStorage);
    }

private:
    std::variant<T, Error> mStorage;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : mError(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return !mError.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] Error const& error() const& {
        if (ok()) throw std::runtime_error("Result<void>::error() called on success");
        return *mError;
    }

private:
    std::optional<Error> mError;
};

inline Result<void> ok() { return {}; }
inline Result<void> fail(std::string message) { return Error(std::move(message)); }

} // namespace litematicabds
