cmake_minimum_required(VERSION 3.20)

project(
    devver
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(devver
    src/devver/main.cpp
)

target_compile_options(devver
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
)

install(
    TARGETS devver
    RUNTIME DESTINATION bin
)