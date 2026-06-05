cmake_minimum_required(VERSION 3.16)

project(sdspidriver C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(sdspidriver STATIC ${CMAKE_CURRENT_LIST_DIR}/src/sd_spi_driver.cpp)
target_include_directories(sdspidriver PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)