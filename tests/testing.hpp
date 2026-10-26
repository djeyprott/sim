// A minimal test harness, so the project builds with no third-party code.
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, tol); }
//
// Run all tests with `histsim_tests`, or a subset with `histsim_tests <substring>`.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::fprintf(stderr, "  %s:%d: %s\n", file, line, what.c_str());
}

template <class A, class B>
std::string describe(const char* expr, const A& a, const B& b) {
    std::ostringstream os;
    os << expr << " (" << a << " vs " << b << ")";
    return os.str();
}

}  // namespace testing

#define HISTSIM_CAT2(a, b) a##b
#define HISTSIM_CAT(a, b) HISTSIM_CAT2(a, b)

#define TEST(name)                                                                              \
    static void HISTSIM_CAT(test_fn_, name)();                                                 \
    static const ::testing::Registrar HISTSIM_CAT(test_reg_, name)(#name, &HISTSIM_CAT(test_fn_, name)); \
    static void HISTSIM_CAT(test_fn_, name)()

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) ::testing::fail(__FILE__, __LINE__, #cond);   \
    } while (0)

#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        const auto& va_ = (a);                                                                    \
        const auto& vb_ = (b);                                                                    \
        if (!(va_ == vb_)) ::testing::fail(__FILE__, __LINE__, ::testing::describe(#a " == " #b, va_, vb_)); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                     \
    do {                                                                                          \
        const double va_ = (a);                                                                   \
        const double vb_ = (b);                                                                   \
        if (!(std::fabs(va_ - vb_) <= (tol)))                                                     \
            ::testing::fail(__FILE__, __LINE__, ::testing::describe(#a " ~= " #b, va_, vb_));    \
    } while (0)
