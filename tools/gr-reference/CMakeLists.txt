add_executable(gr-reference main.cpp)

target_link_libraries(gr-reference PRIVATE spaceflight::core spaceflight::warnings)
# tests/support carries the fixed point-mass ephemeris. A cross-validation tool
# wants exactly that -- a field with no ephemeris error in it -- and duplicating
# the header here would be two copies of the same thing.
target_include_directories(gr-reference PRIVATE "${CMAKE_SOURCE_DIR}")
