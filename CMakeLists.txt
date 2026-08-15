# This project is primarily Python; Python will automatically build the native
cmake_minimum_required(VERSION 4.2.0 FATAL_ERROR)
project(FlashGBX_Native LANGUAGES C CXX)

# components using this, as part of the build
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FlashGBX/LK_Chromatic")