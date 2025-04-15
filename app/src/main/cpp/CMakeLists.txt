cmake_minimum_required(VERSION 3.22.1)
project(rtkprocessor)

# Set C standard and enable exceptions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fexceptions")

# Find Android NDK libraries
find_library(log-lib log)

# Add RTKLIB sources
file(GLOB RTKLIB_SOURCES 
    rtklib/*.c
    rtklib/*.cpp
)

# Add your native library
add_library(rtkprocessor SHARED
    rtk_processor.cpp
    ${RTKLIB_SOURCES}
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/rtklib/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
target_link_libraries(rtkprocessor
    android
    ${log-lib}
)
