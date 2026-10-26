cmake_minimum_required(VERSION 3.21)

project(histsim
    VERSION 0.1.0
    DESCRIPTION "Human history simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link the MSVC C++ runtime statically so the Windows executable runs on any
# machine without the Visual C++ Redistributable installed.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(HISTSIM_BUILD_TESTS "Build unit and determinism tests" ON)
option(HISTSIM_BUILD_CLI "Build the headless command-line runner" ON)
option(HISTSIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Determinism flags.
#
# A run must be bit-identical across compilers and platforms. That rules out
# anything that lets the compiler reorder or fuse floating-point operations:
#   - no -ffast-math / /fp:fast
#   - no fused multiply-add contraction (-ffp-contract=off)
#   - IEEE semantics on MSVC (/fp:precise, no contraction)
# Simulation code must also avoid libm transcendental functions (sin, exp,
# atan2, pow, ...) on any path that feeds simulation state: their results
# differ between C libraries. Only + - * / and sqrt are correctly rounded
# everywhere. See docs/determinism.md.
# ---------------------------------------------------------------------------
add_library(histsim_flags INTERFACE)
if(MSVC)
    # /fp:precise does not contract a*b+c into FMA unless /fp:contract is given.
    target_compile_options(histsim_flags INTERFACE /W4 /permissive- /fp:precise)
    if(HISTSIM_WARNINGS_AS_ERRORS)
        target_compile_options(histsim_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(histsim_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -ffp-contract=off -fno-fast-math)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86|i[3-6]86)$")
        # 32-bit x86 would use x87 extended precision; force SSE2.
        target_compile_options(histsim_flags INTERFACE -msse2 -mfpmath=sse)
    endif()
    if(HISTSIM_WARNINGS_AS_ERRORS)
        target_compile_options(histsim_flags INTERFACE -Werror)
    endif()
endif()

add_subdirectory(libsim)

if(HISTSIM_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(HISTSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
