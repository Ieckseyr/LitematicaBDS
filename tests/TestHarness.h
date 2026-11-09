// 极简测试框架：不引第三方依赖，xmake run litematicabds_tests 直接跑。
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace lit_test {

struct Case {
    char const* name;
    void (*fn)();
};

std::vector<Case>& cases();
int&                failures();
int&                checks();

struct Registrar {
    Registrar(char const* name, void (*fn)()) { cases().push_back({name, fn}); }
};

inline void reportFailure(char const* file, int line, std::string const& message) {
    ++failures();
    std::printf("  FAIL %s:%d  %s\n", file, line, message.c_str());
}

} // namespace lit_test

#define LIT_TEST(name)                                                                                                 \
    static void lit_test_##name();                                                                                     \
    static ::lit_test::Registrar lit_reg_##name(#name, &lit_test_##name);                                              \
    static void lit_test_##name()

#define LIT_CHECK(cond)                                                                                                \
    do {                                                                                                               \
        ++::lit_test::checks();                                                                                        \
        if (!(cond)) ::lit_test::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");                                \
    } while (0)

#define LIT_CHECK_EQ(a, b)                                                                                             \
    do {                                                                                                               \
        ++::lit_test::checks();                                                                                        \
        auto&& lit_a = (a);                                                                                            \
        auto&& lit_b = (b);                                                                                            \
        if (!(lit_a == lit_b)) {                                                                                       \
            ::lit_test::reportFailure(__FILE__, __LINE__, std::string("CHECK_EQ(" #a ", " #b ")"));                    \
        }                                                                                                              \
    } while (0)

#define LIT_CHECK_NEAR(a, b, eps)                                                                                      \
    do {                                                                                                               \
        ++::lit_test::checks();                                                                                        \
        auto lit_a = (a);                                                                                              \
        auto lit_b = (b);                                                                                              \
        auto lit_d = lit_a - lit_b;                                                                                    \
        if (!(lit_d < (eps) && lit_d > -(eps))) {                                                                      \
            ::lit_test::reportFailure(__FILE__, __LINE__, std::string("CHECK_NEAR(" #a ", " #b ")"));                  \
        }                                                                                                              \
    } while (0)
