# FindOCCT.cmake - system OpenCASCADE (OCCT) development libraries
set(OCCT_INC_HINT /usr/include/opencascade)
set(OCCT_LIB_HINT /usr/lib/x86_64-linux-gnu)

find_path(OCCT_INCLUDE_DIR TopoDS_Shape.hxx HINTS ${OCCT_INC_HINT})

set(_OCCT_LIBS
    TKDESTEP TKDEIGES
    TKernel TKMath TKG2d TKG3d
    TKBRep TKGeomBase TKGeomAlgo TKTopAlgo
    TKMesh TKShHealing TKBool TKFeat TKFillet TKOffset TKPrim
    TKService TKV3d TKOpenGl
    TKCAF TKLCAF TKXCAF TKVCAF
    TKBin TKBinL TKBinXCAF TKStd TKStdL
    TKSTEP TKSTEPAttr TKSTEPBase TKSTEP209
    TKIGES TKXDESTEP TKXDEIGES TKXSBase
    TKXCAF TKRWMesh TKSTL TKVRML)

foreach(_lib ${_OCCT_LIBS})
    find_library(OCCT_${_lib}_LIB ${_lib} HINTS ${OCCT_LIB_HINT})
    if(OCCT_${_lib}_LIB)
        list(APPEND OCCT_LIBRARIES ${OCCT_${_lib}_LIB})
    endif()
endforeach()

if(OCCT_INCLUDE_DIR AND OCCT_LIBRARIES)
    set(OCCT_FOUND TRUE)
    message(STATUS "OCCT found at ${OCCT_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "OCCT not found - install libocct-*-dev")
endif()
