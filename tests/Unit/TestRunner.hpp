#pragma once

#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace CPPPdfTest {

class TestFailure final : public std::runtime_error {
public:
    TestFailure(std::string expression, const char* file, int line)
        : std::runtime_error(BuildMessage(std::move(expression), file, line)) {}

private:
    static std::string BuildMessage(std::string expression, const char* file, int line) {
        std::ostringstream stream;
        stream << "check failed: " << expression << " (" << file << ':' << line << ')';
        return stream.str();
    }
};

inline void Check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw TestFailure(expression, file, line);
    }
}

template <typename Value>
inline void CheckNear(const Value actual, const Value expected, const Value tolerance,
                      const char* expression, const char* file, const int line) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::ostringstream stream;
        stream << "check near failed: " << expression << " (got " << actual
               << ", expected " << expected << " within " << tolerance
               << ") (" << file << ':' << line << ')';
        throw TestFailure(stream.str(), file, line);
    }
}

template <typename Callable>
inline void ExpectThrows(const Callable& callable, const char* expression,
                         const char* file, const int line) {
    bool thrown = false;
    try {
        callable();
    } catch (const std::exception&) {
        thrown = true;
    }
    if (!thrown) {
        std::ostringstream stream;
        stream << "expected exception: " << expression
               << " (" << file << ':' << line << ')';
        throw TestFailure(stream.str(), file, line);
    }
}

// Test case: a named function plus an optional group tag. Register through
// TestRunner so failures report the exact test that broke.
class TestRunner final {
public:
    template <typename Callable>
    void Run(std::string_view name, Callable&& callable) {
        ++total_;
        std::cout << "[ RUN      ] " << name << '\n';
        const auto start = std::chrono::steady_clock::now();

        try {
            const int result = Invoke(std::forward<Callable>(callable));
            const auto elapsed = ElapsedMilliseconds(start);
            if (result == 0) {
                ++passed_;
                std::cout << "[       OK ] " << name << " (" << elapsed << " ms)\n";
            } else {
                ++failed_;
                std::cout << "[  FAILED  ] " << name << " (returned " << result
                          << ", " << elapsed << " ms)\n";
            }
        } catch (const std::exception& error) {
            ++failed_;
            std::cout << "[  FAILED  ] " << name << " (" << ElapsedMilliseconds(start)
                      << " ms)\n             " << error.what() << '\n';
        } catch (...) {
            ++failed_;
            std::cout << "[  FAILED  ] " << name << " (" << ElapsedMilliseconds(start)
                      << " ms)\n             unknown exception\n";
        }
    }

    [[nodiscard]] int PrintSummary(std::string_view heading = "PdfPP.UnitTests") const {
        std::cout << "\n========== " << heading << " summary ==========\n"
                  << "Total : " << total_ << '\n'
                  << "Passed: " << passed_ << '\n'
                  << "Failed: " << failed_ << '\n'
                  << "===============================================\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    template <typename Callable>
    static int Invoke(Callable&& callable) {
        if constexpr (std::is_same_v<std::invoke_result_t<Callable>, void>) {
            std::invoke(std::forward<Callable>(callable));
            return 0;
        } else {
            return static_cast<int>(std::invoke(std::forward<Callable>(callable)));
        }
    }

    static long long ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
    }

    std::size_t total_{};
    std::size_t passed_{};
    std::size_t failed_{};
};

} // namespace CPPPdfTest

#define PDFPP_TEST_CHECK(expression) \
    ::CPPPdfTest::Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define PDFPP_TEST_CHECK_NEAR(actual, expected, tolerance) \
    ::CPPPdfTest::CheckNear((actual), (expected), (tolerance), #actual " == " #expected, __FILE__, __LINE__)

#define PDFPP_TEST_EXPECT_THROWS(callable) \
    ::CPPPdfTest::ExpectThrows((callable), #callable, __FILE__, __LINE__)
