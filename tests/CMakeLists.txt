# create executables for unit tests
add_executable(unit_tests
    unit/test_htttp.c 
    # add more when needed
)

message("Building unit tests...")
# link library under test
target_link_libraries(unit_tests PRIVATE corestack)

# register the test with CTest
add_test(NAME unit_tests COMMAND unit_tests)