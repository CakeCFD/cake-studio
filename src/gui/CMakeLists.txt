find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Network)
find_package(VTK 9 REQUIRED COMPONENTS
    CommonCore CommonDataModel RenderingCore RenderingOpenGL2
    InteractionStyle FiltersGeneral FiltersGeometry FiltersCore FiltersFlowPaths
    IOGeometry IOImport IOParallel
    GUISupportQt RenderingAnnotation)

if(NOT TARGET JsonCpp::JsonCpp)
    add_library(JsonCpp::JsonCpp SHARED IMPORTED)
    set_target_properties(JsonCpp::JsonCpp PROPERTIES
        IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libjsoncpp.so
        INTERFACE_INCLUDE_DIRECTORIES /usr/include/jsoncpp)
endif()

set(SOURCES
    main.cpp
    MainWindow.cpp
    ConsolePanel.cpp
    ResidualPlot.cpp
    ViewportWidget.cpp
    MeshWizard.cpp
    CakeMeshPanel.cpp
    ExportResultsDialog.cpp
    RunLogger.cpp
    DatasetClient.cpp
    DatasetPanel.cpp
    ApiFormatDialog.cpp
    CitationManager.cpp
    CaseTreePanel.cpp
    PhysicsPanel.cpp
    PropertiesPanel.cpp
    DomainSetupDialog.cpp
    BCEditorDialog.cpp
    FileBrowserDialog.cpp
    OpenCaseDialog.cpp
    MeshQualityDialog.cpp
    MRFDialog.cpp
    SystemConfigPanel.cpp
    ThermalPropertiesDialog.cpp
    SolverConfigDialog.cpp
    themes/ThemeEngine.cpp)

set(HEADERS
    MainWindow.h
    ConsolePanel.h
    ResidualPlot.h
    ViewportWidget.h
    MeshWizard.h
    CakeMeshPanel.h
    ExportResultsDialog.h
    RunLogger.h
    DatasetClient.h
    DatasetPanel.h
    ApiFormatDialog.h
    CitationManager.h
    CaseTreePanel.h
    PhysicsPanel.h
    PropertiesPanel.h
    DomainSetupDialog.h
    BCEditorDialog.h
    FileBrowserDialog.h
    OpenCaseDialog.h
    MeshQualityDialog.h
    MRFDialog.h
    SystemConfigPanel.h
    ThermalPropertiesDialog.h
    SolverConfigDialog.h
    themes/ThemeEngine.h)

add_executable(CFDStudio ${SOURCES} ${HEADERS})

target_include_directories(CFDStudio PRIVATE
    ${CMAKE_SOURCE_DIR}/src/geometry
    ${CMAKE_SOURCE_DIR}/src/meshing)

target_link_libraries(CFDStudio PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
    ${VTK_LIBRARIES}
    JsonCpp::JsonCpp
    core geometry meshing solver postprocess)

vtk_module_autoinit(
    TARGETS CFDStudio
    MODULES ${VTK_LIBRARIES})
