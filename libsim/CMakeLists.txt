# Embed the content packs in the library as bytes (a string literal would hit
# MSVC's length limit). Re-runs automatically when a JSON file changes.
foreach(_pack species phonology culture)
    string(TOUPPER "${_pack}" _PACK)
    set(_file "${PROJECT_SOURCE_DIR}/data/${_pack}.json")
    file(READ "${_file}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," HISTSIM_${_PACK}_BYTES "${_hex}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
endforeach()
configure_file(src/content_data.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/content_data.cpp" @ONLY)

add_library(histsim
    "${CMAKE_CURRENT_BINARY_DIR}/generated/content_data.cpp"
    src/content.cpp
    src/grid.cpp
    src/interventions.cpp
    src/json.cpp
    src/language.cpp
    src/systems/culture.cpp
    src/rng.cpp
    src/scheduler.cpp
    src/simulation.cpp
    src/state.cpp
    src/world.cpp
    src/systems/climate.cpp
    src/systems/people.cpp
)
add_library(histsim::histsim ALIAS histsim)

target_include_directories(histsim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(histsim PUBLIC cxx_std_20)
target_link_libraries(histsim PRIVATE histsim_flags)
# The Godot extension is a shared library that links this one.
set_target_properties(histsim PROPERTIES POSITION_INDEPENDENT_CODE ON)
