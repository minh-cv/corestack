# create executables for unit tests
enable_testing()

add_executable(unit_tests
    unit/test_htttp.c
    unity/unity.c
    # add more when needed
)

message("Building unit tests...")
# link library under test
target_link_libraries(unit_tests PRIVATE htttp)

target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include unity)

# register the test with CTest
add_test(NAME unit_tests COMMAND unit_tests)