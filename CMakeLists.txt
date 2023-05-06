cmake_minimum_required(VERSION 3.20)

project(PbrtDX)

set(CMAKE_CXX_STANDARD 20)

set(PBRT_SCENES_DIR "D:/pbrt-v4-scenes")

add_subdirectory(cmake)

add_subdirectory(external/rply)

add_subdirectory(src)
