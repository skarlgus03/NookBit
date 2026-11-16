#pragma once

#include <iostream>
#include <source_location>
#include <string_view>

namespace nookbit::test {

inline int failures = 0;

inline void expect(bool condition, std::string_view message,
                   std::source_location location = std::source_location::current()) {
    if (condition) return;
    ++failures;
    std::cerr << location.file_name() << ':' << location.line() << ": FAIL: " << message << '\n';
}

} // namespace nookbit::test
