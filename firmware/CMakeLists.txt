cmake_minimum_required(VERSION 3.20)
project(firmware CXX C ASM)

add_executable(firmware src/main.cpp)

target_compile_options(firmware PRIVATE -Wall -Wextra)