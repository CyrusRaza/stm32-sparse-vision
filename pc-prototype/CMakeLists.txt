cmake_minimum_required(VERSION 3.16)
project(vision_prototype CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED)

add_executable(vision_prototype
    src/main.cpp
    src/motion_detector.cpp
    src/contour_detector.cpp
    src/projection_detector.cpp
    src/cdnet.cpp
)
target_compile_options(vision_prototype PRIVATE -Wall -Wextra)
target_link_libraries(vision_prototype PRIVATE ${OpenCV_LIBS})
target_include_directories(vision_prototype PRIVATE ${OpenCV_INCLUDE_DIRS})
