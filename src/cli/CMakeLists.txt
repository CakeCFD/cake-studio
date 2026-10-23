cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAMLCPP REQUIRED yaml-cpp)

add_executable(cfdstudio
    main.cpp
    RunConfig.cpp
)

target_include_directories(cfdstudio PRIVATE
    ${CMAKE_SOURCE_DIR}/src/geometry
    ${CMAKE_SOURCE_DIR}/src/meshing
    ${YAMLCPP_INCLUDE_DIRS}
)

target_link_libraries(cfdstudio PRIVATE
    geometry
    meshing
    Qt6::Core
    ${YAMLCPP_LIBRARIES}
)

install(TARGETS cfdstudio RUNTIME DESTINATION bin)
