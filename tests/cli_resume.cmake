# Runs the CLI twice: 100 years straight, and 40 years + save + resume 60 years.
# The final hashes must match.
execute_process(COMMAND ${CLI} run --seed 9 --level 4 --years 100 --hash-only
    OUTPUT_VARIABLE straight RESULT_VARIABLE rc1 OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CLI} run --seed 9 --level 4 --years 40 --save ${WORK}/cli_resume.hsim --hash-only
    RESULT_VARIABLE rc2 OUTPUT_QUIET)
execute_process(COMMAND ${CLI} resume ${WORK}/cli_resume.hsim --years 60 --hash-only
    OUTPUT_VARIABLE resumed RESULT_VARIABLE rc3 OUTPUT_STRIP_TRAILING_WHITESPACE)
file(REMOVE ${WORK}/cli_resume.hsim)

if(NOT rc1 EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT rc3 EQUAL 0)
    message(FATAL_ERROR "CLI failed (${rc1}, ${rc2}, ${rc3})")
endif()
if(NOT straight STREQUAL resumed)
    message(FATAL_ERROR "resume mismatch: straight=${straight} resumed=${resumed}")
endif()
message(STATUS "hash ${straight} matches after resume")
