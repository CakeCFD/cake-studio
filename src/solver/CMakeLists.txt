add_library(solver STATIC
    SolverRunner.cpp)

target_include_directories(solver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/schemes/TENO)

target_link_libraries(solver PUBLIC Qt6::Core)

add_subdirectory(schemes/TENO)
