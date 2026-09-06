// Placeholder test: verifies version wiring. Keep tests deterministic,
// no RNG, no network, no platform-specific calls.
#include <cassert>
#include <iostream>

#include "cardengine/cardengine.h"

int main() {
    static_assert(cardengine::kVersionMajor == 0);
    static_assert(cardengine::kVersionMinor == 1);
    static_assert(cardengine::kVersionPatch == 0);
    assert(cardengine::version_string() == "0.1.0");
    std::cout << "test_version ok\n";
    return 0;
}
