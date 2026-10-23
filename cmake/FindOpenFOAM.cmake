if(DEFINED ENV{WM_PROJECT_DIR})
    set(OPENFOAM_ROOT $ENV{WM_PROJECT_DIR})
else()
    set(OPENFOAM_ROOT /usr/lib/openfoam/openfoam2412)
endif()

find_path(OPENFOAM_INCLUDE_DIR
    NAMES fvMesh.H
    PATHS ${OPENFOAM_ROOT}/src/finiteVolume/lnInclude
          ${OPENFOAM_ROOT}/src/OpenFOAM/lnInclude)

if(OPENFOAM_INCLUDE_DIR)
    set(OPENFOAM_FOUND TRUE)
    set(OPENFOAM_INCLUDE_DIRS
        ${OPENFOAM_ROOT}/src/OpenFOAM/lnInclude
        ${OPENFOAM_ROOT}/src/finiteVolume/lnInclude
        ${OPENFOAM_ROOT}/src/meshTools/lnInclude
        ${OPENFOAM_ROOT}/src/transportModels/lnInclude
        ${OPENFOAM_ROOT}/src/TurbulenceModels/turbulenceModels/lnInclude)
    file(GLOB _of_core ${OPENFOAM_ROOT}/platforms/*/lib/libOpenFOAM.so)
    list(GET _of_core 0 _of_core)
    get_filename_component(OPENFOAM_LIB_DIR ${_of_core} DIRECTORY)
    set(OPENFOAM_LIBRARIES
        ${OPENFOAM_LIB_DIR}/libOpenFOAM.so
        ${OPENFOAM_LIB_DIR}/libfiniteVolume.so
        ${OPENFOAM_LIB_DIR}/libmeshTools.so
        ${OPENFOAM_LIB_DIR}/libturbulenceModels.so)
    message(STATUS "OpenFOAM found at ${OPENFOAM_ROOT}")
else()
    message(FATAL_ERROR "OpenFOAM not found")
endif()
