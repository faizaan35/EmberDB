#pragma once

#include <string>
#include <variant>
#include <utility>
#include <stdexcept>
#include <type_traits>

namespace emberdb {

enum class StatusCode {
    OK = 0,
    NotFound,
    AlreadyExists,
    InvalidArgument,
    IOError,
    Corruption,
    InvalidSyntax,
    TypeMismatch,
    TransactionError,
    InternalError
};

class Status {
public:
    Status() : code_(StatusCode::OK), message_("") {}
    Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

    static Status OK() { return Status(); }
    static Status NotFound(std::string message) { return Status(StatusCode::NotFound, std::move(message)); }
    static Status AlreadyExists(std::string message) { return Status(StatusCode::AlreadyExists, std::move(message)); }
    static Status InvalidArgument(std::string message) { return Status(StatusCode::InvalidArgument, std::move(message)); }
    static Status IOError(std::string message) { return Status(StatusCode::IOError, std::move(message)); }
    static Status Corruption(std::string message) { return Status(StatusCode::Corruption, std::move(message)); }
    static Status InvalidSyntax(std::string message) { return Status(StatusCode::InvalidSyntax, std::move(message)); }
    static Status TypeMismatch(std::string message) { return Status(StatusCode::TypeMismatch, std::move(message)); }
    static Status TransactionError(std::string message) { return Status(StatusCode::TransactionError, std::move(message)); }
    static Status InternalError(std::string message) { return Status(StatusCode::InternalError, std::move(message)); }

    bool ok() const { return code_ == StatusCode::OK; }
    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }

    std::string ToString() const;

    bool operator==(const Status& other) const {
        return code_ == other.code_ && message_ == other.message_;
    }
    bool operator!=(const Status& other) const {
        return !(*this == other);
    }

private:
    StatusCode code_;
    std::string message_;
};

template <typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}

    template <typename U, typename = std::enable_if_t<std::is_constructible_v<T, U> && !std::is_same_v<std::decay_t<U>, Status> && !std::is_same_v<std::decay_t<U>, Result<T>>>>
    Result(U&& value) : data_(T(std::forward<U>(value))) {}

    Result(Status status) : data_(std::move(status)) {
        if (std::get<Status>(data_).ok()) {
            throw std::logic_error("Result cannot be initialized with an OK Status instead of a value");
        }
    }

    bool ok() const {
        return std::holds_alternative<T>(data_);
    }

    const Status& status() const {
        if (ok()) {
            static const Status ok_status = Status::OK();
            return ok_status;
        }
        return std::get<Status>(data_);
    }

    T& value() {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value of failed Result: " + status().ToString());
        }
        return std::get<T>(data_);
    }

    const T& value() const {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value of failed Result: " + status().ToString());
        }
        return std::get<T>(data_);
    }

    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

private:
    std::variant<Status, T> data_;
};

} // namespace emberdb
