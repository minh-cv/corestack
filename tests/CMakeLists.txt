cmake_minimum_required(VERSION 3.10)

# enables testing for current directory and below
enable_testing()

# create executables for unit tests
add_executable(unit_tests
    tests/unit/test_htttp.c 
    # add more when needed
)

# link library under test
target_link_libraries(unit_tests PRIVATE corestack)

# register the test with CTest
add_test(NAME unit_tests COMMAND unit_tests)