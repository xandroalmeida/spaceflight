#pragma once

// A small test harness, written rather than vendored, for one reason: rule
// section 32.
//
//   "Nenhum teste cientifico deve simplesmente usar EXPECT_NEAR(a, b, 0.01)
//    sem justificativa."
//
// Every approximate comparison here REQUIRES a justification string as its last
// argument -- it is a macro parameter, so a tolerance without a stated origin
// does not compile.  The justification is printed with the result, which makes
// the test log itself the tolerance documentation.
//
//   CHECK_NEAR_ABS(measured, expected, 25.0,
//                  "DE440 vs Horizons/DE441 differ by 2.4 m for the Moon; 10x margin");
//
// Exit codes: 0 all passed, 1 some failed, 77 everything was skipped (CTest
// reports that as "Skipped" rather than a false green).

#include <functional>
#include <string>
#include <vector>

namespace sft {

// Thrown by REQUIRE_* and SKIP to abandon the current test.
struct AssertionFailed {
    std::string message;
};
struct TestSkipped {
    std::string reason;
};

struct TestCase {
    std::string name;
    std::string file;
    std::function<void()> body;
};

class Registry {
public:
    bool add(const char* name, const char* file, void (*body)());
    int run(const std::string& filter);
    [[nodiscard]] const std::vector<TestCase>& cases() const { return cases_; }

private:
    std::vector<TestCase> cases_;
};

Registry& registry();

// Reporting used by the macros.
void report_pass(const char* expr);
void report_detail(const std::string& line);
void report_failure(const char* file, int line, const std::string& message);

// Comparison helpers, returning the formatted diagnostic.
std::string describe_abs(double value, double expected, double tolerance, const char* why);
std::string describe_rel(double value, double expected, double tolerance, const char* why);
bool near_abs(double value, double expected, double tolerance);
bool near_rel(double value, double expected, double tolerance);

}  // namespace sft

#define TEST(NAME)                                                                       \
    static void NAME();                                                                  \
    static const bool NAME##_sft_registered = ::sft::registry().add(#NAME, __FILE__, NAME); \
    static void NAME()

#define SFT_FAIL(MSG)                                     \
    do {                                                  \
        ::sft::report_failure(__FILE__, __LINE__, (MSG)); \
    } while (false)

#define SFT_FAIL_FATAL(MSG)                               \
    do {                                                  \
        ::sft::report_failure(__FILE__, __LINE__, (MSG)); \
        throw ::sft::AssertionFailed{(MSG)};              \
    } while (false)

#define CHECK(COND)                                    \
    do {                                               \
        if (!(COND)) {                                 \
            SFT_FAIL("CHECK failed: " #COND);          \
        } else {                                       \
            ::sft::report_pass(#COND);                 \
        }                                              \
    } while (false)

#define REQUIRE(COND)                                  \
    do {                                               \
        if (!(COND)) {                                 \
            SFT_FAIL_FATAL("REQUIRE failed: " #COND);  \
        } else {                                       \
            ::sft::report_pass(#COND);                 \
        }                                              \
    } while (false)

#define CHECK_EQ(A, B)                                                             \
    do {                                                                           \
        const auto sft_a = (A);                                                    \
        const auto sft_b = (B);                                                    \
        if (!(sft_a == sft_b)) {                                                   \
            SFT_FAIL(std::string{"CHECK_EQ failed: " #A " == " #B});               \
        } else {                                                                   \
            ::sft::report_pass(#A " == " #B);                                      \
        }                                                                          \
    } while (false)

// value, expected, absolute tolerance, WHY the tolerance has that value.
#define CHECK_NEAR_ABS(VALUE, EXPECTED, TOL, WHY)                                        \
    do {                                                                                 \
        const double sft_v = (VALUE);                                                    \
        const double sft_e = (EXPECTED);                                                 \
        const double sft_t = (TOL);                                                      \
        const std::string sft_d = ::sft::describe_abs(sft_v, sft_e, sft_t, (WHY));       \
        if (!::sft::near_abs(sft_v, sft_e, sft_t)) {                                     \
            SFT_FAIL(std::string{#VALUE " vs " #EXPECTED "\n      "} + sft_d);           \
        } else {                                                                         \
            ::sft::report_detail(std::string{#VALUE "  "} + sft_d);                      \
        }                                                                                \
    } while (false)

#define CHECK_NEAR_REL(VALUE, EXPECTED, TOL, WHY)                                        \
    do {                                                                                 \
        const double sft_v = (VALUE);                                                    \
        const double sft_e = (EXPECTED);                                                 \
        const double sft_t = (TOL);                                                      \
        const std::string sft_d = ::sft::describe_rel(sft_v, sft_e, sft_t, (WHY));       \
        if (!::sft::near_rel(sft_v, sft_e, sft_t)) {                                     \
            SFT_FAIL(std::string{#VALUE " vs " #EXPECTED "\n      "} + sft_d);           \
        } else {                                                                         \
            ::sft::report_detail(std::string{#VALUE "  "} + sft_d);                      \
        }                                                                                \
    } while (false)

#define REQUIRE_NEAR_REL(VALUE, EXPECTED, TOL, WHY)                                      \
    do {                                                                                 \
        const double sft_v = (VALUE);                                                    \
        const double sft_e = (EXPECTED);                                                 \
        const double sft_t = (TOL);                                                      \
        const std::string sft_d = ::sft::describe_rel(sft_v, sft_e, sft_t, (WHY));       \
        if (!::sft::near_rel(sft_v, sft_e, sft_t)) {                                     \
            SFT_FAIL_FATAL(std::string{#VALUE " vs " #EXPECTED "\n      "} + sft_d);     \
        } else {                                                                         \
            ::sft::report_detail(std::string{#VALUE "  "} + sft_d);                      \
        }                                                                                \
    } while (false)

#define INFO(MSG) ::sft::report_detail(MSG)

#define SKIP(REASON) throw ::sft::TestSkipped{(REASON)}

#define CHECK_THROWS_AS(EXPR, EXCEPTION)                                          \
    do {                                                                          \
        bool sft_threw = false;                                                   \
        try {                                                                     \
            (void)(EXPR);                                                         \
        } catch (const EXCEPTION&) {                                              \
            sft_threw = true;                                                     \
        } catch (...) {                                                           \
            SFT_FAIL("threw the wrong exception type: " #EXPR);                   \
            break;                                                                \
        }                                                                         \
        if (!sft_threw) {                                                         \
            SFT_FAIL("expected " #EXCEPTION " from: " #EXPR);                     \
        } else {                                                                  \
            ::sft::report_pass(#EXPR " throws " #EXCEPTION);                      \
        }                                                                         \
    } while (false)
