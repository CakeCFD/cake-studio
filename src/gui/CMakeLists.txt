find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL Network)
find_package(VTK 9 REQUIRED COMPONENTS
    CommonCore CommonDataModel RenderingCore RenderingOpenGL2
    InteractionStyle FiltersGeneral FiltersGeometry FiltersCore FiltersFlowPaths
    IOGeometry IOImport IOParallel
    GUISupportQt RenderingAnnotation)

set(SOURCES
    main.cpp
    MainWindow.cpp
    ConsolePanel.cpp
    ResidualPlot.cpp
    ViewportWidget.cpp
    FlowParticles.cpp
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
    FlowParticles.h
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
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::Network
    ${VTK_LIBRARIES}
    JsonCpp::JsonCpp
    core geometry meshing solver postprocess)

vtk_module_autoinit(
    TARGETS CFDStudio
    MODULES ${VTK_MODULES_USED})
