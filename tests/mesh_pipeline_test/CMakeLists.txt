include(FindOCCT)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(VTK 9 REQUIRED COMPONENTS CommonCore CommonDataModel FiltersGeneral IOGeometry)

add_executable(mesh_pipeline_test mesh_pipeline_test.cpp)

target_include_directories(mesh_pipeline_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/geometry
    ${CMAKE_SOURCE_DIR}/src/meshing
    ${OCCT_INCLUDE_DIR})

target_link_libraries(mesh_pipeline_test PRIVATE
    geometry meshing Qt6::Core
    ${OCCT_LIBRARIES} ${VTK_LIBRARIES})
