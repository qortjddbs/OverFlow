cmake_minimum_required(VERSION 3.16)
project(OverFlowServer LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OverFlowServer uses Winsock IOCP and only builds on Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(OverFlowServer src/main.cpp)
target_link_libraries(OverFlowServer PRIVATE ws2_32)
