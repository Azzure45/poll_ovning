cmake_minimum_required(VERSION 3.10.0)
project(server VERSION 0.1.0 LANGUAGES C CXX)

add_executable(server robust_server.cpp)

include(CTest)
enable_testing()

