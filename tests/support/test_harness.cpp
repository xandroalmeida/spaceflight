#include "tests/support/test_harness.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace sft {
namespace {

bool g_current_failed = false;
int g_failure_count = 0;

}  // namespace

Registry& registry() {
    static Registry instance;
    return instance;
}

bool Registry::add(const char* name, const char* file, void (*body)()) {
    cases_.push_back(TestCase{name, file, body});
    return true;
}

void report_pass(const char* expr) {
    std::cout << "    ok    " << expr << "\n";
}

void report_detail(const std::string& line) {
    std::cout << "    ok    " << line << "\n";
}

void report_failure(const char* file, int line, const std::string& message) {
    g_current_failed = true;
    ++g_failure_count;
    std::cout << "    FAIL  " << message << "\n          at " << file << ":" << line << "\n";
}

bool near_abs(double value, double expected, double tolerance) {
    if (!std::isfinite(value) || !std::isfinite(expected)) {
        return false;
    }
    return std::abs(value - expected) <= tolerance;
}

bool near_rel(double value, double expected, double tolerance) {
    if (!std::isfinite(value) || !std::isfinite(expected)) {
        return false;
    }
    if (expected == 0.0) {
        return std::abs(value) <= tolerance;
    }
    return std::abs(value - expected) / std::abs(expected) <= tolerance;
}

std::string describe_abs(double value, double expected, double tolerance, const char* why) {
    std::ostringstream os;
    const double error = value - expected;
    os << std::setprecision(12) << "= " << value << ", expected " << expected
       << ", abs error " << std::abs(error);
    if (expected != 0.0) {
        os << " (rel " << std::abs(error / expected) << ")";
    }
    os << ", tolerance " << tolerance << "\n          tolerance from: " << why;
    return os.str();
}

std::string describe_rel(double value, double expected, double tolerance, const char* why) {
    std::ostringstream os;
    const double error = value - expected;
    const double rel = expected != 0.0 ? std::abs(error / expected) : std::abs(error);
    os << std::setprecision(12) << "= " << value << ", expected " << expected << ", rel error " << rel
       << " (abs " << std::abs(error) << "), tolerance " << tolerance
       << "\n          tolerance from: " << why;
    return os.str();
}

int Registry::run(const std::string& filter) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const auto& test : cases_) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            continue;
        }

        std::cout << "\n[ RUN  ] " << test.name << "\n";
        g_current_failed = false;

        try {
            test.body();
        } catch (const TestSkipped& skip) {
            std::cout << "[ SKIP ] " << test.name << ": " << skip.reason << "\n";
            ++skipped;
            continue;
        } catch (const AssertionFailed&) {
            // Already reported by the macro.
        } catch (const std::exception& e) {
            report_failure(test.file.c_str(), 0, std::string{"unexpected exception: "} + e.what());
        } catch (...) {
            report_failure(test.file.c_str(), 0, "unexpected non-standard exception");
        }

        if (g_current_failed) {
            std::cout << "[ FAIL ] " << test.name << "\n";
            ++failed;
        } else {
            std::cout << "[  OK  ] " << test.name << "\n";
            ++passed;
        }
    }

    std::cout << "\n----------------------------------------------------------\n"
              << passed << " passed, " << failed << " failed, " << skipped << " skipped"
              << " (" << g_failure_count << " assertion failure(s))\n";

    if (failed > 0) {
        return 1;
    }
    if (passed == 0 && skipped > 0) {
        return 77;  // CTest SKIP_RETURN_CODE: no data available, nothing was verified
    }
    return 0;
}

}  // namespace sft

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    return sft::registry().run(filter);
}
