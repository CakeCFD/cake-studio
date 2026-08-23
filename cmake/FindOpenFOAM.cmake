set(OPENFOAM_ROOT /usr/lib/openfoam/openfoam2412)

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
    set(OPENFOAM_LIB_DIR ${OPENFOAM_ROOT}/platforms/linux64GccDPInt32Opt/lib)
    set(OPENFOAM_LIBRARIES
        ${OPENFOAM_LIB_DIR}/libOpenFOAM.so
        ${OPENFOAM_LIB_DIR}/libfiniteVolume.so
        ${OPENFOAM_LIB_DIR}/libmeshTools.so
        ${OPENFOAM_LIB_DIR}/libturbulenceModels.so)
    message(STATUS "OpenFOAM found at ${OPENFOAM_ROOT}")
else()
    message(FATAL_ERROR "OpenFOAM not found")
endif()
