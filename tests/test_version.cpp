// Version wiring test: the kVersion* constants come from the CMake-generated
// cardengine/version.h (single-sourced from project() VERSION). Keep tests
// deterministic, no RNG, no network, no platform-specific calls.
#include <cassert>
#include <iostream>
#include <string>

#include "cardengine/cardengine.h"

int main() {
    // Tripwire literals: update these alongside the project() VERSION bump.
    static_assert(cardengine::kVersionMajor == 0);
    static_assert(cardengine::kVersionMinor == 5);
    static_assert(cardengine::kVersionPatch == 0);
    assert(cardengine::version_string() == "0.5.0");
    // Header-vs-project() agreement: the CARDENGINE_PROJECT_VERSION_* macros
    // are injected by CMake from PROJECT_VERSION_* (identical by
    // construction; guards future hand-edits of either side).
    static_assert(cardengine::kVersionMajor == CARDENGINE_PROJECT_VERSION_MAJOR);
    static_assert(cardengine::kVersionMinor == CARDENGINE_PROJECT_VERSION_MINOR);
    static_assert(cardengine::kVersionPatch == CARDENGINE_PROJECT_VERSION_PATCH);
    const std::string expected =
        std::to_string(CARDENGINE_PROJECT_VERSION_MAJOR) + "." +
        std::to_string(CARDENGINE_PROJECT_VERSION_MINOR) + "." +
        std::to_string(CARDENGINE_PROJECT_VERSION_PATCH);
    assert(cardengine::version_string() == expected);
    std::cout << "test_version ok\n";
    return 0;
}
