#pragma once

// Umbrella header: pulls in the whole engine API.
#include "cardengine/bot.h"
#include "cardengine/bot_runner.h"
#include "cardengine/card.h"
#include "cardengine/championship.h"
#include "cardengine/clock.h"
#include "cardengine/config.h"
#include "cardengine/deck.h"
#include "cardengine/event.h"
#include "cardengine/game_file.h"
#include "cardengine/hand.h"
#include "cardengine/protocol.h"
#include "cardengine/session_file.h"
#include "cardengine/table.h"
#include "cardengine/tournament.h"

#include <string>

namespace cardengine {

// Keep in sync with the project() VERSION in the root CMakeLists.txt.
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 5;
inline constexpr int kVersionPatch = 0;

// Human-readable "major.minor.patch".
std::string version_string();

}  // namespace cardengine
