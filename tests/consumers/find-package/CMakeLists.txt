# An app that uses an installed copy of Ninefold through find_package.
cmake_minimum_required(VERSION 3.25)
project(consumer LANGUAGES C)

find_package(ninefold REQUIRED)

add_executable(consumer ../main.c)
target_link_libraries(consumer PRIVATE ninefold::ninefold)
