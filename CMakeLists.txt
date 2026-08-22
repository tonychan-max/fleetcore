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

# ---- common: the wire contract plus the OS primitives it rides on ----
# Now has implementation files, so it is a real static library.
add_library(fc_common STATIC
    common/wire.cpp
    common/msgq.cpp
    common/process.cpp
    common/stream_buffer.cpp
    common/tcp_server.cpp
)

target_include_directories(fc_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(fc_common PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ---- layout_dump: measures the actual struct layout (ADR-0003) ----
# Not a throwaway script. Re-run it whenever a field is added.
add_executable(layout_dump common/layout_dump.cpp)
target_link_libraries(layout_dump PRIVATE fc_common)

# ---- msggen: produces a real byte stream for testing ----
add_executable(msggen tools/msggen/main.cpp)
target_link_libraries(msggen PRIVATE fc_common)

# ---- business processes (ARCHITECTURE section 3-2) ----
add_executable(gateway gateway/main.cpp)
target_link_libraries(gateway PRIVATE fc_common)

add_executable(locator locator/main.cpp)
target_link_libraries(locator PRIVATE fc_common)

add_executable(termd termd/main.cpp)
target_link_libraries(termd PRIVATE fc_common)