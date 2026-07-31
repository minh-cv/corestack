add_library(unity unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)

add_executable(test_logger test_logger.c)
target_link_libraries(test_logger PRIVATE unity logger)
add_test(NAME logger COMMAND test_logger)
