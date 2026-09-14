#pragma once

// Reading the project's own source, for the small class of invariants that are
// about SHAPE rather than about numbers.
//
// Most rules in this project are checked by measuring something.  A few are not
// measurable that way: "the GDExtension does not solve orbits" is a statement
// about which calls exist, and a program that starts making them can agree with
// the core for a long time before it stops -- which is exactly how Milestone 6
// ended up with two planners that both looked fine.
//
// So those are checked by reading the file.  The path comes from CMake rather
// than from the working directory, because a test's working directory is
// wherever ctest happened to be invoked from.

#include "tests/support/test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef SPACEFLIGHT_SOURCE_DIR
#define SPACEFLIGHT_SOURCE_DIR "."
#endif

namespace sft {

// Path is relative to the repository root.  A missing file is a FAILURE, not a
// skip: it means the thing being checked has moved, and a check that quietly
// stops checking is worse than no check.
inline std::string read_repository_file(const std::string& relative_path) {
    const std::filesystem::path full =
        std::filesystem::path{SPACEFLIGHT_SOURCE_DIR} / relative_path;
    std::ifstream in{full};
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace sft
