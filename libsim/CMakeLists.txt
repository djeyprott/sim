add_library(histsim
    src/grid.cpp
    src/rng.cpp
    src/scheduler.cpp
    src/simulation.cpp
    src/state.cpp
    src/world.cpp
    src/systems/foragers.cpp
)
add_library(histsim::histsim ALIAS histsim)

target_include_directories(histsim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(histsim PUBLIC cxx_std_20)
target_link_libraries(histsim PRIVATE histsim_flags)
