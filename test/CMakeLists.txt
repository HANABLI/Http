# CMakeList.txt for Http unit tests
#
# © 2024 by Hatem Nabli

cmake_minimum_required(VERSION 3.20)
set(this HttpTests)

set(SOURCES 
    src/ClientTests.cpp 
    src/ServerTests.cpp
)

add_executable(${this} ${SOURCES})
set_target_properties(${this} PROPERTIES 
    FOLDER Tests
)

target_include_directories(${this} PRIVATE ..)

target_link_libraries(${this}
    gtest_main
    Http
    Uri
)

add_test(
    NAME ${this} 
    COMMAND ${this}
)