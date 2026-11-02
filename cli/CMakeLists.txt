add_executable(histsim_cli main.cpp)
set_target_properties(histsim_cli PROPERTIES OUTPUT_NAME histsim)
target_link_libraries(histsim_cli PRIVATE histsim::histsim histsim::view histsim_flags)
target_compile_definitions(histsim_cli PRIVATE HISTSIM_VERSION="${PROJECT_VERSION}")

# Offline tool: bakes Earth's relief into data/earth/elevation_l7.bin.
add_executable(histsim_bake_earth ../tools/bake_earth.cpp)
target_link_libraries(histsim_bake_earth PRIVATE histsim::histsim histsim_flags)
