#include <cstdio>
#include <cstring>

#include "testing.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& c : testing::registry()) {
        if (filter && !std::strstr(c.name, filter)) {
            continue;
        }
        const int before = testing::failures();
        c.body();
        ++run;
        std::printf("%s %s\n", testing::failures() == before ? "[ ok ]" : "[FAIL]", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, testing::failures());
    return testing::failures() == 0 && run > 0 ? 0 : 1;
}
