#pragma once

// Shared test utilities. Every suite links stdlib only, so the helpers are
// header-inline. Failure output stays uniform: `FAIL: ...` plus exit 1.
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "cardengine/card.h"

namespace testutil {

inline void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

inline void check(bool condition, const std::string& message) {
    check(condition, message.c_str());
}

inline bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Expects body() to throw Expected (and only Expected).
template <typename Expected, typename F>
void expect_throws(F&& run, const char* message) {
    // The second catch is dead code when Expected is std::exception itself,
    // which warnings-as-errors rejects: hence the split.
    if constexpr (std::is_same_v<Expected, std::exception>) {
        try {
            run();
        } catch (const std::exception&) {
            return;
        }
    } else {
        try {
            run();
        } catch (const Expected&) {
            return;
        } catch (const std::exception& e) {
            std::cerr << "FAIL (wrong exception type: " << e.what()
                      << "): " << message << "\n";
            std::exit(1);
        }
    }
    std::cerr << "FAIL (expected exception): " << message << "\n";
    std::exit(1);
}

inline std::vector<cardengine::Card> cards(
    std::initializer_list<const char*> texts) {
    std::vector<cardengine::Card> out;
    for (const char* text : texts) out.push_back(cardengine::parse_card(text));
    return out;
}

}  // namespace testutil
