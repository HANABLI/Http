# CMackeLists.txt for Http Module
#
# © 2024 by Hatem Nabli


cmake_minimum_required(VERSION 3.8)
set(this Http)

set(Headers 
    include/Http/Server.hpp
    include/Http/Client.hpp
)

set(Sources 
    src/Server.cpp 
    src/Client.cpp
)

add_library(${this} STATIC ${Sources} ${Headers})
set_target_properties(${this} PROPERTIES 
    FOLDER Libraries
)

target_include_directories(${this} PUBLIC include)

target_link_libraries(${this} PUBLIC
    MessageHeaders
    Uri
)

add_subdirectory(test)