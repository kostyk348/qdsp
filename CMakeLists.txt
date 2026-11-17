cmake_minimum_required(VERSION 3.16)
project(qdsp C)

# Библиотека header-only: собирать нечего, проверяются тесты и демо.
option(QDSP_BUILD_TESTS "Собрать unit-тесты" ON)
option(QDSP_BUILD_DEMO  "Собрать демо"       ON)

if(QDSP_BUILD_TESTS)
    enable_testing()
    add_executable(test_qdsp tests/test_qdsp.c)
    target_include_directories(test_qdsp PRIVATE include)
    set_target_properties(test_qdsp PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    if(NOT MSVC)
        target_compile_options(test_qdsp PRIVATE -Wall -Wextra -Werror -O2)
    endif()
    target_link_libraries(test_qdsp PRIVATE m)
    add_test(NAME qdsp_unit COMMAND test_qdsp)
endif()

if(QDSP_BUILD_DEMO)
    add_executable(qdsp_demo examples/demo.c)
    target_include_directories(qdsp_demo PRIVATE include)
    target_link_libraries(qdsp_demo PRIVATE m)
endif()
