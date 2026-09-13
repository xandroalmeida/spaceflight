# Warning policy for code we own.  Third-party code (CSPICE) is compiled with -w
# by cmake/cspice.cmake; see ADR-0001.
add_library(spaceflight_warnings INTERFACE)
add_library(spaceflight::warnings ALIAS spaceflight_warnings)

if(MSVC)
    target_compile_options(spaceflight_warnings INTERFACE /W4 /permissive-)
    if(SPACEFLIGHT_WERROR)
        target_compile_options(spaceflight_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(spaceflight_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wconversion -Wsign-conversion
        -Wdouble-promotion
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wnull-dereference
        -Wformat=2)
    if(SPACEFLIGHT_WERROR)
        target_compile_options(spaceflight_warnings INTERFACE -Werror)
    endif()
endif()
