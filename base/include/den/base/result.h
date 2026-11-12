// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace den {

/// Outcome of an operation. `notes` carries things the user should know even when it
/// succeeded (for example a file that was left alone).
struct Status {
    bool ok = true;
    std::string error;
    std::vector<std::string> notes;

    static Status Fail(std::string message) {
        return Status{false, std::move(message), {}};
    }
    explicit operator bool() const {
        return ok;
    }
};

/// A value or the reason there is none.
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Status failure) : status_(std::move(failure)) {
        if (status_.ok) {
            status_ = Status::Fail("no value");
        }
    }
    static Result Fail(std::string message) {
        return Result(Status::Fail(std::move(message)));
    }

    explicit operator bool() const {
        return value_.has_value();
    }
    bool ok() const {
        return value_.has_value();
    }
    const std::string& error() const {
        return status_.error;
    }
    const Status& status() const {
        return status_;
    }
    T& value() {
        return *value_;
    }
    const T& value() const {
        return *value_;
    }
    T* operator->() {
        return &*value_;
    }
    T& operator*() {
        return *value_;
    }
    T take() {
        return std::move(*value_);
    }

private:
    std::optional<T> value_;
    Status status_;
};

} // namespace den
