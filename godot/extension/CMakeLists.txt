# GDExtension: exposes the simulation core and map rendering to Godot.
# Built only when HISTSIM_BUILD_GODOT is ON. The library is written into
# godot/project/bin/, where histsim.gdextension expects it.

include(FetchContent)

# The core reports errors with exceptions, so godot-cpp must not disable them.
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
# One build serves both the editor and exported games.
set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)

FetchContent_Declare(godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG godot-4.5-stable
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(godot-cpp)

add_library(histsim_godot SHARED
    src/histsim_world.cpp
    src/histsim_world.hpp
    src/register_types.cpp
)
target_link_libraries(histsim_godot PRIVATE godot-cpp histsim::view)
target_compile_features(histsim_godot PRIVATE cxx_std_20)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(HISTSIM_GODOT_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(HISTSIM_GODOT_PLATFORM macos)
else()
    set(HISTSIM_GODOT_PLATFORM linux)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(HISTSIM_GODOT_ARCH arm64)
else()
    set(HISTSIM_GODOT_ARCH x86_64)
endif()

if(WIN32)
    set_target_properties(histsim_godot PROPERTIES PREFIX "")
else()
    set_target_properties(histsim_godot PROPERTIES PREFIX "lib")
endif()

set(HISTSIM_GODOT_BIN "${PROJECT_SOURCE_DIR}/godot/project/bin")
set_target_properties(histsim_godot PROPERTIES
    OUTPUT_NAME "histsim_godot.${HISTSIM_GODOT_PLATFORM}.${HISTSIM_GODOT_ARCH}"
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    BUILD_RPATH_USE_ORIGIN ON
    # Generator expressions stop multi-config generators adding Release/ subfolders.
    LIBRARY_OUTPUT_DIRECTORY "$<1:${HISTSIM_GODOT_BIN}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${HISTSIM_GODOT_BIN}>")
