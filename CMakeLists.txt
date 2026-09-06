cmake_minimum_required(VERSION 3.28)

project(cardengine VERSION 0.1.0 LANGUAGES CXX)

# Portable ISO C++ only: no compiler extensions, no platform-only APIs in core.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict: warnings are errors on every target, every platform.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

include(GNUInstallDirs)

add_library(cardengine STATIC
    src/cardengine.cpp
    src/card.cpp
    src/deck.cpp
    src/hand.cpp
    src/config.cpp
    src/table.cpp
    src/game_file.cpp
    src/protocol.cpp
    src/bot.cpp
    src/bot_runner.cpp
    src/event.cpp
    src/tournament.cpp
)
target_include_directories(cardengine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Portable warnings; MSVC uses /W4, others use -Wall -Wextra -Wpedantic.
if(MSVC)
    target_compile_options(cardengine PRIVATE /W4)
else()
    target_compile_options(cardengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cardengine_app src/main.cpp)
set_target_properties(cardengine_app PROPERTIES OUTPUT_NAME cardengine)
target_link_libraries(cardengine_app PRIVATE cardengine)
if(MSVC)
    target_compile_options(cardengine_app PRIVATE /W4)
else()
    target_compile_options(cardengine_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cardengine_bot src/bot_main.cpp)
target_link_libraries(cardengine_bot PRIVATE cardengine)
if(MSVC)
    target_compile_options(cardengine_bot PRIVATE /W4)
else()
    target_compile_options(cardengine_bot PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS cardengine cardengine_app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
