add_executable(orbit-cli
    main.cpp
    json.cpp
    scenario.cpp
)

target_link_libraries(orbit-cli PRIVATE spaceflight::core spaceflight::warnings)
target_include_directories(orbit-cli PRIVATE "${CMAKE_SOURCE_DIR}")
