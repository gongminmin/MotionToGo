cmake_minimum_required(VERSION 3.18 FATAL_ERROR)

set(motion_to_go_cmake_module_dir "${CMAKE_CURRENT_SOURCE_DIR}/CMake/Modules")
list(APPEND CMAKE_MODULE_PATH "${motion_to_go_cmake_module_dir}")

set(motion_to_go_major_version 0)
set(motion_to_go_minor_version 1)
set(motion_to_go_patch_version 0)
set(motion_to_go_version ${motion_to_go_major_version}.${motion_to_go_minor_version}.${motion_to_go_patch_version})

project(MotionToGo VERSION ${motion_to_go_version})

include(${motion_to_go_cmake_module_dir}/Common.cmake)
include(${motion_to_go_cmake_module_dir}/Platform.cmake)
include(${motion_to_go_cmake_module_dir}/Compiler.cmake)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

add_subdirectory(External)
add_subdirectory(Source)
add_subdirectory(Test)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "MotionToGo")
