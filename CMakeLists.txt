cmake_minimum_required(VERSION 3.10)
project(LogMonitor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

add_executable(LogMonitor 
    src/main.cpp
)

target_include_directories(LogMonitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Для отладки
if(MSVC)
    target_compile_options(LogMonitor PRIVATE /W4)
else()
    target_compile_options(LogMonitor PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()