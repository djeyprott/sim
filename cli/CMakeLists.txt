add_executable(histsim_cli main.cpp)
set_target_properties(histsim_cli PROPERTIES OUTPUT_NAME histsim)
target_link_libraries(histsim_cli PRIVATE histsim::histsim histsim_flags)
target_compile_definitions(histsim_cli PRIVATE HISTSIM_VERSION="${PROJECT_VERSION}")
