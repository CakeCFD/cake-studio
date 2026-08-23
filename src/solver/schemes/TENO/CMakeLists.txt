# TENO scheme is header-only (templated) - compiled into solver lib
# Just expose include path
add_library(teno_scheme INTERFACE)
target_include_directories(teno_scheme INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
