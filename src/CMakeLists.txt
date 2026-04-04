cmake_minimum_required(VERSION 3.10)
project(RaftGame)

set(CMAKE_CXX_STANDARD 17)

# Find raylib
find_package(raylib QUIET)

if (NOT raylib_FOUND)
    # Try pkg-config
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(RAYLIB raylib)
    endif()
endif()

add_executable(raft_game main.cpp)

if (raylib_FOUND)
    target_link_libraries(raft_game raylib)
elseif (RAYLIB_FOUND)
    target_include_directories(raft_game PRIVATE ${RAYLIB_INCLUDE_DIRS})
    target_link_libraries(raft_game ${RAYLIB_LIBRARIES})
else()
    # Manual fallback
    target_link_libraries(raft_game raylib m pthread dl)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(raft_game m pthread dl)
endif()
