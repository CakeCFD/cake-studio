include(FindOCCT)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(VTK 9 REQUIRED COMPONENTS CommonCore CommonDataModel FiltersGeneral)

add_library(geometry STATIC
    CADImporter.cpp
    CADTessellator.cpp
    CADGeometry.cpp
)

target_include_directories(geometry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OCCT_INCLUDE_DIR})

target_link_libraries(geometry PUBLIC
    Qt6::Core
    ${OCCT_LIBRARIES}
    ${VTK_LIBRARIES})
