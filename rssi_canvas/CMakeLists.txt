cmake_minimum_required(VERSION 3.16)
project(rssi_canvas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(raylib 4.0 REQUIRED)

add_executable(rssi_canvas src/main.cpp)
target_link_libraries(rssi_canvas PRIVATE raylib)
