# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(gmsslLib)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

add_library(gmcurl SHARED napi_gmcurl.cpp)
target_link_libraries(gmcurl PUBLIC libace_napi.z.so hilog_ndk.z.so)
target_link_libraries(gmcurl PUBLIC  ${NATIVERENDER_ROOT_PATH}/../../../libs/${OHOS_ARCH}/libcurl.so.4)
