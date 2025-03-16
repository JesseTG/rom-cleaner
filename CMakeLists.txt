cmake_minimum_required(VERSION 3.11)

cmake_policy(VERSION 3.11)
message(STATUS "Using CMake ${CMAKE_VERSION} from ${CMAKE_COMMAND}" )
if (CMAKE_MESSAGE_LOG_LEVEL)
    message(STATUS "Message log level is set to ${CMAKE_MESSAGE_LOG_LEVEL}")
endif ()

project("ROM cleaner"
        VERSION "0.0.0"
        DESCRIPTION "A utility core for removing dust and dirt from your ROM collection."
        HOMEPAGE_URL "https://jesse.tg"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" "${CMAKE_MODULE_PATH}")
option(KISSFFT_STATIC "" ON)
option(KISSFFT_TEST "" OFF)
option(KISSFFT_PKGCONFIG "" OFF)
set(KISSFFT_DATATYPE "int16_t")
option(KISSFFT_TOOLS "" OFF)

include(FetchContent)
include(CheckSymbolExists)
include(cmake/FetchDependencies.cmake)
include(cmake/ConfigureFeatures.cmake)
include(cmake/libretro-common.cmake)

add_library(romcleaner_libretro MODULE
    libretro.cpp
    pntr.c
)

add_subdirectory(assets)

add_common_definitions(romcleaner_libretro)
add_common_definitions(libretro-common)

add_compile_definitions(
    PNTR_ENABLE_TTF
    PNTR_ENABLE_FILTER_SMOOTH
    PNTR_ENABLE_MATH
    PNTR_ENABLE_VARGS
    PNTR_PIXELFORMAT_ARGB
    STB_IMAGE_STATIC # To avoid using any dynamically-linked STB implementations
    STBI_NO_THREAD_LOCALS
)

target_include_directories(romcleaner_libretro SYSTEM PUBLIC
    "${libretro-common_SOURCE_DIR}/include"
    "${pntr_SOURCE_DIR}"
    "${kissfft_SOURCE_DIR}/include"
)

# libretro cores do not start with "lib"
set_target_properties(romcleaner_libretro PROPERTIES PREFIX "")

# Some platforms or compilers don't use the expected suffixes for shared libraries
if(EMSCRIPTEN AND LIBRETRO_STATIC)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}.bc")
elseif (LIBRETRO_SUFFIX AND LIBRETRO_STATIC)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}${CMAKE_STATIC_LIBRARY_SUFFIX}")
elseif (LIBRETRO_SUFFIX)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}${CMAKE_SHARED_LIBRARY_SUFFIX}")
elseif (APPLE)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX ".dylib")
elseif(ANDROID)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX "_android.so")
elseif (UNIX)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX ".so")
elseif (WIN32)
    set_target_properties(romcleaner_libretro PROPERTIES SUFFIX ".dll")
endif ()

if (WIN32 AND MINGW)
    target_link_options(romcleaner_libretro PUBLIC -static-libgcc -static-libstdc++ -static)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "(.+)?Clang")
    target_compile_options(romcleaner_libretro PUBLIC -Werror=return-type)
    # For some reason, C++ allows functions to not return values in all code paths.
    # This has tripped me up before, so I'm forcing it to be an error.
endif()

target_link_libraries(romcleaner_libretro PUBLIC libretro-common pntr kissfft)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Defining DEBUG in romcleaner_libretro and libretro-common targets")
    target_compile_definitions(romcleaner_libretro PUBLIC DEBUG)
    target_compile_definitions(libretro-common PUBLIC DEBUG)
endif ()