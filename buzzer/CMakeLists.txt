# 顶层 CMakeLists.txt：整个工程的入口
# 注意：必须在 project() 之前 include project.cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32s2_buzzer_demo)
