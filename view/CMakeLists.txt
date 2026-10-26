# Display layer: map modes, color scales, rasterizing and PNG output.
# Used by the CLI's map command and the Godot viewer. Display only: nothing
# here feeds simulation state, so the determinism rules do not apply.
add_library(histsim_view
    src/color.cpp
    src/map_modes.cpp
    src/png.cpp
    src/raster.cpp
)
add_library(histsim::view ALIAS histsim_view)
target_include_directories(histsim_view PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(histsim_view PUBLIC histsim::histsim PRIVATE histsim_flags)
set_target_properties(histsim_view PROPERTIES POSITION_INDEPENDENT_CODE ON)
