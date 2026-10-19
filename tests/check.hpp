#pragma once

#include <print>
#include <source_location>
#include <string_view>

namespace ugh::test {

/// Minimal assertion collector: reports every failed check and keeps going.
class Checker {
public:
    void operator()(bool condition, std::string_view what,
                    std::source_location where = std::source_location::current())
    {
        if (!condition) {
            std::println(stderr, "FAIL {}:{}: {}", where.file_name(), where.line(), what);
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const { return failures_; }

private:
    int failures_ = 0;
};

} // namespace ugh::test
