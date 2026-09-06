#include <iostream>

#include "cardengine/cardengine.h"
#include "cardengine/protocol.h"

// CardEngine speaks the text protocol from docs/PROTOCOL.md on stdin/stdout,
// UCI-style: any program in any language can drive it. The first line is a
// banner; everything after is command in, reply out.
int main() {
    // Flushed: clients read the banner through a pipe before sending input.
    std::cout << "cardengine " << cardengine::version_string() << "\n"
              << std::flush;
    return cardengine::run_protocol(std::cin, std::cout);
}
