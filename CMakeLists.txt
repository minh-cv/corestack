cmake_minimum_required(VERSION 3.10)
project(tetrish LANGUAGES C)


option(TETRISH_DEV_MODE "Enable dev-mode macros" OFF)
option(TETRISH_TETRISD_NO_DAEMON "Disable daemonization of tetrisd" ${TETRISH_DEV_MODE})


add_compile_options(-g -Wall -Wextra -Wpedantic -Wconversion -Wno-strict-prototypes)


add_library(htttp src/libhtttp/htttp.c)
target_include_directories(htttp PUBLIC include)


add_library(tetrissh src/libtetrissh/tetrissh.c)
target_include_directories(tetrissh PUBLIC include)
target_link_libraries(tetrissh PUBLIC common)


add_library(common src/common/common.c)
target_include_directories(common PUBLIC include)
target_link_libraries(common PUBLIC crypto)


add_library(config src/config/config.c)
target_include_directories(config PUBLIC include)


add_library(server src/server/client_io.c src/server/client_auth.c)
target_include_directories(server PUBLIC src/server include)
target_link_libraries(server PUBLIC tetrissh)
target_compile_definitions(server PUBLIC
    $<$<BOOL:${TETRISH_TETRISD_NO_DAEMON}>:TETRISH_TETRISD_NO_DAEMON>
)


# add_executable(tetrisu src/tetrisu/main.c
#     src/tetrisu/config_var.c)
# target_include_directories(tetrisu PRIVATE src/tetrisu)


install(
    TARGETS
        htttp
        tetrissh
        common
        config
        server
    RUNTIME DESTINATION bin           # for executables (and DLLs on Windows)
    LIBRARY DESTINATION lib           # for shared libraries on Unix
    ARCHIVE DESTINATION lib           # for static libs (.a, .lib)
)
