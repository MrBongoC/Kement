cmake_minimum_required(VERSION 3.15)
project(KementCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Kement.Core SHARED
    src/dllmain.cpp
    src/KementProfiler.cpp
    src/Kement.Core.def
)

target_include_directories(Kement.Core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(Kement.Core PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WINDOWS
    _USRDLL
)

# CoreCLR profiler headers assume the target/host bitness matches the runtime.
# Build x64 to match a 64-bit .NET process.
target_link_libraries(Kement.Core PRIVATE bcrypt)

# Unit test for the PE reconstruction (flat + synthetic mapped layout).
add_executable(pe_dump_test tests/pe_dump_test.cpp)
target_include_directories(pe_dump_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(pe_dump_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(pe_dump_test PRIVATE bcrypt)

set_target_properties(Kement.Core PROPERTIES
    OUTPUT_NAME "Kement.Core"
    PREFIX ""
)
