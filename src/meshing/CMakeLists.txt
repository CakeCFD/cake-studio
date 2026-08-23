find_package(Qt5 REQUIRED COMPONENTS Core)
find_package(VTK 9 REQUIRED COMPONENTS CommonCore FiltersGeneral IOXML IOGeometry)

add_library(meshing STATIC
    SurfaceMeshWriter.cpp
    BlockMeshWriter.cpp
    SnappyConfigWriter.cpp
    CaseGenerator.cpp
)

target_include_directories(meshing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(meshing PUBLIC
    Qt5::Core
    geometry
    ${VTK_LIBRARIES})
