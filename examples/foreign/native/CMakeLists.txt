# An upstream library keeps its native build description and public interface.
cmake_minimum_required(VERSION 3.21)
project(Greeting LANGUAGES CXX)
add_library(greeting STATIC greeting.cpp)
add_library(Greeting::Greeting ALIAS greeting)
target_include_directories(greeting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(greeting PUBLIC cxx_std_17)
