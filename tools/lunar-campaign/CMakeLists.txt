add_executable(lunar-campaign
    main.cpp
    ../orbit-cli/scenario.cpp
)

target_link_libraries(lunar-campaign PRIVATE spaceflight::core spaceflight::warnings)
target_include_directories(lunar-campaign PRIVATE "${CMAKE_SOURCE_DIR}")
