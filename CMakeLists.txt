cmake_minimum_required(VERSION 3.16)

project(fleetcore
        VERSION 0.1.0
        DESCRIPTION "Contract-driven command & telemetry backbone"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable compiler-specific extensions (-std=c++17, not -std=gnu++17).
# ADR-0001 requires the same source to build on Windows; GNU extensions
# would silently break that and we would only find out much later.
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- common: the single source of truth for the wire contract ----
# Header-only for now. Switch to STATIC once implementation files exist (Step 2).
add_library(fc_common INTERFACE)

target_include_directories(fc_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(fc_common INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ---- layout_dump: measures the actual struct layout (ADR-0003) ----
# Not a throwaway script. Re-run it whenever a field is added.
add_executable(layout_dump common/layout_dump.cpp)
target_link_libraries(layout_dump PRIVATE fc_common)