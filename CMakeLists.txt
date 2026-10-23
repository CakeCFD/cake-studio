cmake_minimum_required(VERSION 3.20)
project(CFDStudio VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

find_package(MPI QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

if(NOT TARGET JsonCpp::JsonCpp)
    find_library(JSONCPP_LIB jsoncpp HINTS ${JSONCPP_LIBRARY_DIRS} REQUIRED)
    add_library(JsonCpp::JsonCpp SHARED IMPORTED GLOBAL)
    set_target_properties(JsonCpp::JsonCpp PROPERTIES
        IMPORTED_LOCATION ${JSONCPP_LIB}
        INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_INCLUDE_DIRS}")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)
find_package(VTK 9 REQUIRED COMPONENTS
    CommonCore CommonDataModel RenderingCore RenderingOpenGL2
    InteractionStyle FiltersGeneral IOGeometry IOImport
    GUISupportQt RenderingAnnotation)

include(FindOpenFOAM)

add_subdirectory(src/core)
add_subdirectory(src/geometry)
add_subdirectory(src/meshing)
add_subdirectory(src/solver)
add_subdirectory(src/postprocess)
add_subdirectory(src/gui)
add_subdirectory(tests)

add_subdirectory(src/cli)
