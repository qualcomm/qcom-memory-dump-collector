cmake_minimum_required(VERSION 3.15)
project(CrashCollection VERSION 0.1.0 LANGUAGES CXX)

# Set output directories
if(WIN32)
    set(PLATFORM_NAME ${CMAKE_VS_PLATFORM_NAME})
else()
    set(PLATFORM_NAME ${CMAKE_SYSTEM_PROCESSOR})
endif()

set(QMDC_OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/build/${CMAKE_SYSTEM_NAME}/${PLATFORM_NAME}")
if(WIN32)
    # Windows uses multi-config generators, so keep the configuration below
    # each artifact category: {bin,lib,dll}/<config>.
    set(QMDC_STATIC_OUTPUT_DIRECTORY "${QMDC_OUTPUT_ROOT}/lib/$<CONFIG>")
    set(QMDC_DLL_OUTPUT_DIRECTORY "${QMDC_OUTPUT_ROOT}/dll/$<CONFIG>")
    set(QMDC_BIN_OUTPUT_DIRECTORY "${QMDC_OUTPUT_ROOT}/bin/$<CONFIG>")
else()
    # Linux builds use CMAKE_BUILD_TYPE at configure time and keep artifacts
    # directly under the platform directory without a configuration level.
    set(QMDC_STATIC_OUTPUT_DIRECTORY "${QMDC_OUTPUT_ROOT}/lib")
    set(QMDC_DLL_OUTPUT_DIRECTORY "${QMDC_OUTPUT_ROOT}/dll")
    set(QMDC_BIN_OUTPUT_DIRECTORY "${QMDC_OUTPUT_ROOT}/bin")
endif()

# Keep static archives in lib/. Shared-library binaries and their import
# libraries are placed in the sibling dll/ directory. The $<CONFIG> suffix
# keeps multi-config generators (for example Visual Studio) separated.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${QMDC_STATIC_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${QMDC_DLL_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QMDC_BIN_OUTPUT_DIRECTORY}")

# ----------------------------------------------------------------------
# Global language settings – C++17 required
# ----------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------
# Platform‑specific flags (Visual Studio 2019)
# ----------------------------------------------------------------------
if (WIN32)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD /O2")
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /MDd /RTC1")

    # Enable correct C++ standard macro reporting (/Zc:__cplusplus)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")

    # Global Windows definitions (shared across all targets)
    if(PLATFORM_NAME MATCHES "ARM64")
        message(STATUS "Configuring for ARM64 architecture")
        add_definitions(-DTOOLS_TARGET_WINDOWS -DQFS_IMGMGMT -DTOOLS_ARCH_ARM64
                        -DWIN32 -DNDEBUG -D_LIB -D_UNICODE -DUNICODE -DNOMINMAX)
        set(CMAKE_GENERATOR_PLATFORM "ARM64")
    elseif(PLATFORM_NAME MATCHES "Win32")
        message(STATUS "Configuring for Win32 (x86) architecture")
        add_definitions(-DTOOLS_TARGET_WINDOWS -DQFS_IMGMGMT -DTOOLS_ARCH_32BIT
                        -DWIN32 -DNDEBUG -D_LIB -D_UNICODE -DUNICODE -DNOMINMAX)
        set(CMAKE_GENERATOR_PLATFORM "Win32")
    else()
        message(STATUS "Configuring for x64 architecture")
        add_definitions(-DTOOLS_TARGET_WINDOWS -DQFS_IMGMGMT -DTOOLS_ARCH_64BIT
                        -DWIN32 -DNDEBUG -D_LIB -D_UNICODE -DUNICODE -DNOMINMAX)
        set(CMAKE_GENERATOR_PLATFORM "x64")
    endif()
    set(CMAKE_GENERATOR "Visual Studio 16 2019")
    set(CMAKE_GENERATOR_TOOLSET "v142")
endif()

# ----------------------------------------------------------------------
# Paths to internal components
# ----------------------------------------------------------------------
set(INTERNAL_QDS_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/src/qds")
set(INTERNAL_KLOGGER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/external/kLogger")

set(EXTERNAL_QDS_DIR    "${INTERNAL_QDS_DIR}")
set(EXTERNAL_KLOGGER_DIR "${INTERNAL_KLOGGER_DIR}")

# ==============================================================================
# SYSTEM DEPENDENCIES
# ==============================================================================

if(UNIX)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
	find_package(Threads REQUIRED)

	find_library(UDEV_LIBRARY udev)
	if(NOT UDEV_LIBRARY)
		message(FATAL_ERROR "libudev not found. Install with: sudo apt-get install libudev-dev")
	endif()

	find_library(UUID_LIBRARY uuid)
	if(NOT UUID_LIBRARY)
		message(FATAL_ERROR "libuuid not found. Install with: sudo apt-get install uuid-dev")
	endif()

elseif(WIN32)
endif()

# ==============================================================================
# INCLUDE DIRECTORY DEFINITIONS
# ==============================================================================

# Core QFS includes (for CrashCollection library - full access)
set(QFS_CORE_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/exports"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# SDK headers are in protected src/sdk directory (not built, header-only reference if needed)
# Note: Per CLAUDE.md, src/sdk/ is a protected directory and should not be modified

# QDS – platform specific + the “common” folder that qcom‑libusb.cpp expects
set(QDS_INCLUDES)
if (WIN32)
    list(APPEND QDS_INCLUDES "${EXTERNAL_QDS_DIR}/qds_win")
else()
    list(APPEND QDS_INCLUDES "${EXTERNAL_QDS_DIR}/qds_lnx/inc")
endif()
list(APPEND QDS_INCLUDES "${EXTERNAL_QDS_DIR}/common")
list(APPEND QDS_INCLUDES "${EXTERNAL_QDS_DIR}/libusb/source")

# ----------------------------------------------------------------------
# kLogger – header‑only INTERFACE target
# ----------------------------------------------------------------------
add_library(kLogger INTERFACE)
target_include_directories(kLogger INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/external/kLogger/include"
)

# Combined includes for CrashCollection library (full access)
set(CRASHCOLLECTION_LIB_INCLUDES
	${QFS_CORE_INCLUDES}
	${QDS_INCLUDES}
)

# CLI executable includes (minimal restricted access)
set(CLI_INCLUDES
	${CMAKE_CURRENT_SOURCE_DIR}/src/cli
)


# ==============================================================================
# SOURCE FILE COLLECTIONS
# ==============================================================================

# CrashCollection library sources (excluding CLI and TestApplication)
file(GLOB_RECURSE CRASHCOLLECTION_SOURCES "src/*.cpp" "src/*.h")
# Filter out CLI and QDS sources – we only want the SDK code
set(TEMP_SOURCES)
foreach(src ${CRASHCOLLECTION_SOURCES})
    if (NOT src MATCHES "cli|qds")
        list(APPEND TEMP_SOURCES "${src}")
    endif()
endforeach()
set(CRASHCOLLECTION_SOURCES ${TEMP_SOURCES})

file(GLOB_RECURSE CLI_SOURCES "src/cli/*.cpp")

# ----------------------------------------------------------------------
# QDS library target
# ----------------------------------------------------------------------
file(GLOB_RECURSE QDS_COMMON_SOURCES
    "${EXTERNAL_QDS_DIR}/common/*.cpp"
    "${EXTERNAL_QDS_DIR}/common/*.h"
    "${EXTERNAL_QDS_DIR}/libusb/source/*.cpp"
    "${EXTERNAL_QDS_DIR}/libusb/source/*.h"
)
if(WIN32)
    file(GLOB_RECURSE QDS_PLATFORM_SOURCES
        "${EXTERNAL_QDS_DIR}/qds_win/*.cpp"
        "${EXTERNAL_QDS_DIR}/qds_win/*.h"
    )
elseif(UNIX)
    file(GLOB_RECURSE QDS_PLATFORM_SOURCES
        "${EXTERNAL_QDS_DIR}/qds_lnx/libLNX/*.cpp"
    )
endif()

add_library(QcDevice STATIC
    ${QDS_COMMON_SOURCES}
    ${QDS_PLATFORM_SOURCES}
    "${EXTERNAL_QDS_DIR}/EUD/QdsEud.cpp"
    "${EXTERNAL_QDS_DIR}/EUD/QdsEud.h"
)
target_include_directories(QcDevice PRIVATE ${QDS_INCLUDES})

if(WIN32)
    target_include_directories(QcDevice PRIVATE
        "${EXTERNAL_QDS_DIR}"
        "${EXTERNAL_QDS_DIR}/libusb/lib-1.0.27/include"
    )
    target_compile_definitions(QcDevice PRIVATE
        TOOLS_TARGET_WINDOWS
        _UNICODE
        UNICODE
        WIN32
        NOMINMAX
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    )
elseif(UNIX)
    target_include_directories(QcDevice PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(QcDevice PRIVATE ${LIBUSB_LIBRARIES})
    target_compile_options(QcDevice PRIVATE -fPIC -fpermissive -Wno-narrowing -Wno-write-strings)
    target_compile_definitions(QcDevice PRIVATE TOOLS_TARGET_LINUX)
endif()

# ----------------------------------------------------------------------
# SDK library targets
# ----------------------------------------------------------------------
# Both library variants are built from the same SDK sources. The CLI links
# only CrashCollectionLib, while CrashCollectionLibShared is available for
# consumers that need the DLL/shared-library API.
function(configure_crashcollection_target target)
    target_include_directories(${target} PUBLIC
        ${QFS_CORE_INCLUDES}
        ${QDS_INCLUDES}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/external/kLogger/include"
    )

    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        LINKER_LANGUAGE CXX
        OUTPUT_NAME "CrashCollectionLib"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    target_include_directories(${target} PRIVATE ${CRASHCOLLECTION_LIB_INCLUDES})

    if(WIN32)
        # Match Visual Studio project preprocessor definitions.
        target_compile_definitions(${target} PRIVATE
            _HAS_STD_BYTE=0
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            NOGDI
            $<$<CONFIG:Debug>:_DEBUG>
            $<$<CONFIG:Release>:NDEBUG>
            _CONSOLE
        )
    endif()

    if(UNIX)
        # AddressSanitizer flags for Debug builds.
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${target} PRIVATE -fPIC -fpermissive -fsanitize=address -fno-omit-frame-pointer -g)
            target_link_options(${target} PRIVATE -fsanitize=address)
        else()
            target_compile_options(${target} PRIVATE -fPIC -fpermissive)
        endif()
        target_compile_definitions(${target} PRIVATE TOOLS_TARGET_LINUX)
        target_link_libraries(${target} PUBLIC
            ${LIBUSB_LIBRARIES}
            ${UDEV_LIBRARY}
            ${UUID_LIBRARY}
            pthread
            uuid
            udev
            kLogger
        )
        target_include_directories(${target} PRIVATE ${LIBUSB_INCLUDE_DIRS})
    elseif(WIN32)
        target_link_libraries(${target} PUBLIC
            ws2_32
            kLogger
        )
    endif()
endfunction()

# Static library used by qmdc. QMDC_STATIC keeps the public headers from
# declaring imported DLL symbols while compiling the executable.
add_library(CrashCollectionLib STATIC ${CRASHCOLLECTION_SOURCES})
configure_crashcollection_target(CrashCollectionLib)
target_compile_definitions(CrashCollectionLib PUBLIC QMDC_STATIC)
set_target_properties(CrashCollectionLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${QMDC_STATIC_OUTPUT_DIRECTORY}"
)

# Shared library for SDK consumers. On Windows, both the DLL and its import
# library are intentionally emitted into dll/ so they cannot collide with
# CrashCollectionLib.lib in lib/.
add_library(CrashCollectionLibShared SHARED ${CRASHCOLLECTION_SOURCES})
configure_crashcollection_target(CrashCollectionLibShared)
# Export the C and C++ APIs from the same DLL using qmdc_export.h.
target_compile_definitions(CrashCollectionLibShared PRIVATE QMDC_DLL_EXPORTS)
target_link_libraries(CrashCollectionLibShared PRIVATE QcDevice)
if(WIN32)
    # Let /MD or /MDd select the matching CRT for the configuration.
    target_link_libraries(CrashCollectionLibShared PRIVATE
        Wtsapi32 Userenv Rpcrt4 Setupapi Shlwapi
    )
elseif(UNIX)
    target_link_libraries(CrashCollectionLibShared PRIVATE dl)
endif()
set_target_properties(CrashCollectionLibShared PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${QMDC_DLL_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${QMDC_DLL_OUTPUT_DIRECTORY}"
    ARCHIVE_OUTPUT_DIRECTORY "${QMDC_DLL_OUTPUT_DIRECTORY}"
)

# ==============================================================================
# CLI EXECUTABLE TARGET
# ==============================================================================

# ----------------------------------------------------------------------
# CLI executable – original VS solution built it with C++17
# ----------------------------------------------------------------------
add_executable(qmdc ${CLI_SOURCES})
set_target_properties(qmdc PROPERTIES
    OUTPUT_NAME "qmdc"
    RUNTIME_OUTPUT_DIRECTORY "${QMDC_BIN_OUTPUT_DIRECTORY}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(qmdc PRIVATE ${CLI_INCLUDES})

add_dependencies(qmdc CrashCollectionLib)

# Platform-specific preprocessor definitions for CLI
if(WIN32)
	# Match Visual Studio project preprocessor definitions
	target_compile_definitions(qmdc PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		NOGDI
		_CRT_SECURE_NO_WARNINGS
		$<$<CONFIG:Debug>:_DEBUG>
		$<$<CONFIG:Release>:NDEBUG>
	)
    target_link_options(qmdc PRIVATE "-NODEFAULTLIB:libcmt.lib")
endif()

# ==============================================================================
# LINKING CONFIGURATION
# ==============================================================================

if(WIN32)
    target_link_libraries(qmdc PUBLIC
        CrashCollectionLib
        QcDevice
        Wtsapi32 Userenv Rpcrt4 Setupapi Shlwapi msvcrt ws2_32
        kLogger)
elseif(UNIX)
    # Remove static linking of libstdc++ to avoid heap corruption with dynamic libraries
    # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

    # AddressSanitizer flags for Debug builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(qmdc PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(qmdc PRIVATE -fsanitize=address)
    endif()

    target_link_libraries(qmdc PUBLIC
        CrashCollectionLib
        QcDevice
        pthread dl uuid udev
        kLogger)
    target_compile_definitions(qmdc PRIVATE TOOLS_TARGET_LINUX)
endif()
