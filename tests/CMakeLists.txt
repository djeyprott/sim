add_executable(histsim_tests
    main.cpp
    test_grid.cpp
    test_rng.cpp
    test_simulation.cpp
    test_world.cpp
)
target_link_libraries(histsim_tests PRIVATE histsim::histsim histsim_flags)

add_test(NAME unit COMMAND histsim_tests)

if(HISTSIM_BUILD_CLI)
    # End-to-end: run, save, resume, and compare with an uninterrupted run.
    add_test(NAME cli_resume_matches
        COMMAND ${CMAKE_COMMAND}
            -DCLI=$<TARGET_FILE:histsim_cli>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_resume.cmake)
endif()
