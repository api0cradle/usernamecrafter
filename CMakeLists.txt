cmake_minimum_required(VERSION 3.31)

project(username-crafter)

add_executable(username-crafter)

add_subdirectory(src)

enable_testing()
add_subdirectory(test)