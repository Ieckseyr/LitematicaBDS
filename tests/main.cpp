#include "TestHarness.h"

#include <cstdio>

namespace lit_test {
std::vector<Case>& cases() {
    static std::vector<Case> value;
    return value;
}
int& failures() {
    static int value = 0;
    return value;
}
int& checks() {
    static int value = 0;
    return value;
}
} // namespace lit_test

int main() {
    std::printf("[litematicabds] %zu tests\n", lit_test::cases().size());
    for (auto const& test : lit_test::cases()) {
        std::printf("- %s\n", test.name);
        try {
            test.fn();
        } catch (std::exception const& e) {
            ++lit_test::failures();
            std::printf("  FAIL exception: %s\n", e.what());
        } catch (...) {
            ++lit_test::failures();
            std::printf("  FAIL unknown exception\n");
        }
    }
    std::printf(
        "[litematicabds] checks=%d failures=%d\n",
        lit_test::checks(),
        lit_test::failures()
    );
    return lit_test::failures() == 0 ? 0 : 1;
}
