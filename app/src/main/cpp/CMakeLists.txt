cmake_minimum_required(VERSION 3.22.1)
project(rtkprocessor)

# Set C standard and enable exceptions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fexceptions")

# Find Android NDK libraries
#find_library(log-lib log)
# Add NDK JNI headers
include_directories(
        ${CMAKE_ANDROID_NDK}/sysroot/usr/include  # Add NDK headers
        ${CMAKE_CURRENT_SOURCE_DIR}/rtklib
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add RTKLIB sources
file(GLOB RTKLIB_SOURCES
        )

# Add your native library
add_library(rtkprocessor SHARED
    rtk_processor.cpp
        rtklib/srtkpp.cpp
        rtklib/rtkcmn.c
        rtklib/rinex.c
        rtklib/rtkpos.c
        rtklib/postpos.c
        rtklib/solution.c
        rtklib/lambda.c
        rtklib/sbas.c
        rtklib/preceph.c
        rtklib/pntpos.c
        rtklib/geoid.c
        rtklib/ephemeris.c
        rtklib/options.c
        rtklib/ppp.c
        rtklib/ppp_ar.c
        rtklib/rtcm.c
        rtklib/rtcm2.c
        rtklib/rtcm3.c
        rtklib/rtcm3e.c
        rtklib/ionex.c
        rtklib/tides.c
        )

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/rtklib
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
target_link_libraries(rtkprocessor
    android
    log
)
