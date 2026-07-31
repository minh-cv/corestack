add_library(unity unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)

add_executable(test_logger test_logger.c)
target_link_libraries(test_logger PRIVATE unity logger)
add_test(NAME logger COMMAND test_logger)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE unity config)
add_test(NAME config COMMAND test_config)

