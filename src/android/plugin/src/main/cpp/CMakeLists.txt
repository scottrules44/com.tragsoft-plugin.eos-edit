# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

project(EOSSDK)

cmake_minimum_required(VERSION 3.4.1)

set(BASE_PROJECT_DIR ${CMAKE_SOURCE_DIR}/../../..)
set(BASE_PLUGIN_DIR ${BASE_PROJECT_DIR}/../..)
set(EOSSDK_DIR ${BASE_PROJECT_DIR}/libs/)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

include_directories(
        ${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include
        ${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include/Corona
        ${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include/lua
        ${BASE_PLUGIN_DIR}/Source
        ${BASE_PLUGIN_DIR}/Dependencies/Epic/public/epic
)

# Add Corona and Lua as prebuilt shared libraries
add_library(EOSSDK SHARED IMPORTED)
add_library(corona SHARED IMPORTED)
add_library(lua SHARED IMPORTED)
set_property(TARGET EOSSDK PROPERTY IMPORTED_LOCATION ${BASE_PROJECT_DIR}/build/unpacked/eos-sdk/jni/${ANDROID_ABI}/libEOSSDK.so)
set_property(TARGET corona PROPERTY IMPORTED_LOCATION ${BASE_PROJECT_DIR}/build/unpacked/Corona/jni/${ANDROID_ABI}/libcorona.so)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${BASE_PROJECT_DIR}/build/unpacked/Corona/jni/${ANDROID_ABI}/liblua.so)

file(GLOB EOSSDK_SOURCE "${BASE_PLUGIN_DIR}/Source/*.h")
file(GLOB CORONA_SOURCE "${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include/Corona/*.h")
file(GLOB LUA_SOURCE "${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include/lua/*.h")
set_property(TARGET EOSSDK PROPERTY INTERFACE_INCLUDE_DIRECTORIES
#        ${EOSSDK_SOURCE}
#        ${CORONA_SOURCE}
#        ${LUA_SOURCE}
        ${BASE_PLUGIN_DIR}/Source
        ${BASE_PLUGIN_DIR}/Dependencies/Epic/public/epic
#        ${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include/Corona
#        ${BASE_PLUGIN_DIR}/Dependencies/Corona/shared/include/lua
)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

file(GLOB EOSSDK_SOURCE_CPP "${BASE_PLUGIN_DIR}/Source/*.cpp")
list(REMOVE_ITEM EOSSDK_SOURCE_CPP
        ${BASE_PLUGIN_DIR}/Source/PlatformCommandLine-win.cpp
)
list(APPEND EOSSDK_SOURCE_CPP "native-lib.cpp")

add_library( # Sets the name of the library.
        native-lib

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        ${EOSSDK_SOURCE_CPP})

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log )

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        native-lib
        
        #Links the target library to the EOSSDK
        corona lua EOSSDK

        # Links the target library to the log library
        # included in the NDK.
        ${log-lib} )
