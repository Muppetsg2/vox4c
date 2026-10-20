cmake_minimum_required(VERSION 3.25...3.31)

# The version is read from the header so there is only one place to bump it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/vox4c.h" _vox4c_version_line
     REGEX "vox4c\\.h - v[0-9]+\\.[0-9]+\\.[0-9]+" LIMIT_COUNT 1)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _vox4c_version "${_vox4c_version_line}")
if(NOT _vox4c_version)
    set(_vox4c_version 0.0.0)
endif()

# LANGUAGES NONE: the header-only target needs no compiler. C is enabled only
# for the static library, tests and examples, so a C++-only parent project
# that just uses vox4c::vox4c stays C++-only.
project(vox4c
    VERSION ${_vox4c_version}
    DESCRIPTION "Single-header MagicaVoxel (.vox) loader for C99 / C++"
    HOMEPAGE_URL "https://github.com/Muppetsg2/vox4c"
    LANGUAGES NONE)

# Top level build (developing vox4c itself) or consumed through
# add_subdirectory / CPM / FetchContent?
set(VOX4C_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

option(VOX4C_BUILD_LIBRARY  "Build vox4c::lib, a static library containing the implementation" OFF)
option(VOX4C_NO_STDIO       "vox4c::lib: build without <stdio.h> (no v4c_load_file)"          OFF)
option(VOX4C_BUILD_TESTS    "Build the vox4c tests"                       ${VOX4C_IS_TOP_LEVEL})
option(VOX4C_BUILD_EXAMPLES "Build the vox4c examples"                    ${VOX4C_IS_TOP_LEVEL})
option(VOX4C_INSTALL        "Generate the install target"                 ${VOX4C_IS_TOP_LEVEL})
option(VOX4C_SANITIZE       "Build vox4c's targets with sanitizers (ASan + UBSan, MSVC: ASan)" OFF)
option(VOX4C_WARNINGS_AS_ERRORS "Treat warnings in vox4c's own targets as errors" OFF)
option(VOX4C_ENABLE_CLANG_TIDY  "Add clang-tidy targets for vox4c's own targets (needs clang-tidy)" OFF)
if(VOX4C_IS_TOP_LEVEL AND WIN32)
    set(_vox4c_vs_launch_default ON)
else()
    set(_vox4c_vs_launch_default OFF)
endif()
option(VOX4C_VS_LAUNCH_CONFIG "Write .vs/launch.vs.json (Visual Studio Open Folder) for the tests and examples" ${_vox4c_vs_launch_default})
set(VOX4C_VOX_FILES_DIR "${PROJECT_SOURCE_DIR}/vox_files" CACHE PATH "Folder searched recursively for *.vox files by the vox4c_* run targets and CTest")
# clang-tidy reads the compile commands of the build; they have to be
# exported before any target is created
set(_vox4c_clang_tidy OFF)
if(VOX4C_ENABLE_CLANG_TIDY)
    if(NOT VOX4C_IS_TOP_LEVEL)
        message(STATUS "vox4c: VOX4C_ENABLE_CLANG_TIDY is ignored when vox4c is not the top-level project")
    else()
        if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
            message(WARNING "vox4c: clang-tidy needs compile_commands.json, which only the Ninja and Makefile "
                            "generators write (not ${CMAKE_GENERATOR})")
        endif()
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
        set(_vox4c_clang_tidy ON)
    endif()
endif()

set(_vox4c_needs_c OFF)
if(VOX4C_BUILD_LIBRARY OR VOX4C_BUILD_TESTS OR VOX4C_BUILD_EXAMPLES)
    set(_vox4c_needs_c ON)
    enable_language(C)
    include(${PROJECT_SOURCE_DIR}/cmake/Vox4cCompilerFlags.cmake)
	if(_vox4c_clang_tidy)
        include(${PROJECT_SOURCE_DIR}/cmake/Vox4cClangTidy.cmake)
    endif()
    if(VOX4C_SANITIZE AND MSVC)
        # MSVC's AddressSanitizer does not work together with the runtime checks
        foreach(_lang C CXX)
            string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_${_lang}_FLAGS_DEBUG "${CMAKE_${_lang}_FLAGS_DEBUG}")
        endforeach()
    endif()
endif()

# ---------------------------------------------------------------------------
# vox4c::vox4c - header only (INTERFACE)
#
#   target_link_libraries(my_app PRIVATE vox4c::vox4c)
#
# and in exactly ONE source file of my_app:
#
#   #define VOX4C_IMPLEMENTATION
#   #include <vox4c.h>
#
# No target_compile_features(c_std_99) on purpose: the header is also valid
# C++, and a C compile feature on the interface breaks C++-only consumers.
# ---------------------------------------------------------------------------
add_library(vox4c INTERFACE)
add_library(vox4c::vox4c ALIAS vox4c)
# the installed include folder is added by install(TARGETS ... INCLUDES DESTINATION)
target_include_directories(vox4c INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>)

# ---------------------------------------------------------------------------
# vox4c::lib - the implementation compiled once, always a STATIC library
#
#   target_link_libraries(my_app PRIVATE vox4c::lib)
#
# and just #include <vox4c.h> - without VOX4C_IMPLEMENTATION (defining it is
# reported with a clear #error, as it would duplicate every symbol).
# ---------------------------------------------------------------------------
if(VOX4C_BUILD_LIBRARY)
    # STATIC explicitly: independent of BUILD_SHARED_LIBS
    add_library(vox4c_lib STATIC "${PROJECT_SOURCE_DIR}/vox4c.c" "${PROJECT_SOURCE_DIR}/vox4c.h")
    add_library(vox4c::lib ALIAS vox4c_lib)
    set_target_properties(vox4c_lib PROPERTIES
        OUTPUT_NAME vox4c
        EXPORT_NAME lib)
    target_include_directories(vox4c_lib PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>)
    # users of the library see VOX4C_PREBUILT, the library itself does not
    target_compile_definitions(vox4c_lib INTERFACE VOX4C_PREBUILT)
    if(VOX4C_NO_STDIO)
        # PUBLIC: the header seen by the users must not declare v4c_load_file either
        target_compile_definitions(vox4c_lib PUBLIC V4C_NO_STDIO)
    endif()
    vox4c_configure_static_library(vox4c_lib)
	if(_vox4c_clang_tidy)
        vox4c_enable_clang_tidy(vox4c_lib)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests and examples
# ---------------------------------------------------------------------------
if(VOX4C_BUILD_TESTS OR VOX4C_BUILD_EXAMPLES)
    include(${PROJECT_SOURCE_DIR}/cmake/Vox4cVoxFiles.cmake)
endif()

# Visual Studio debug configurations (launch.vs.json). Only for a top-level
# build: the file is written into <source>/.vs of the top-level project.
set(_vox4c_vs_launch OFF)
if(VOX4C_VS_LAUNCH_CONFIG AND VOX4C_IS_TOP_LEVEL AND (VOX4C_BUILD_TESTS OR VOX4C_BUILD_EXAMPLES))
    set(_vox4c_vs_launch ON)
    include(${PROJECT_SOURCE_DIR}/cmake/VsLaunch.cmake)
    # .vox files passed to the debug configurations. On Windows CONFIGURE_DEPENDS
    # re-runs CMake when files are added or removed, so the configurations follow.
    # Not elsewhere: a '"' in a file name (impossible on Windows) makes CMake's
    # glob check report a change on every build, an endless re-configure loop.
    set(_vox4c_glob_depends "")
    if(CMAKE_HOST_WIN32)
        set(_vox4c_glob_depends CONFIGURE_DEPENDS)
    endif()
    file(GLOB_RECURSE VOX4C_VS_LAUNCH_FILES LIST_DIRECTORIES false ${_vox4c_glob_depends}
         "${VOX4C_VOX_FILES_DIR}/*.vox")
    include(${PROJECT_SOURCE_DIR}/cmake/Vox4cSortFiles.cmake)
    vox4c_sort_vox_files(VOX4C_VS_LAUNCH_FILES "${VOX4C_VOX_FILES_DIR}" ${VOX4C_VS_LAUNCH_FILES})
endif()

if(VOX4C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VOX4C_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(_vox4c_clang_tidy)
    vox4c_finalize_clang_tidy()
endif()

# Write Visual Studio Launch json file
if(_vox4c_vs_launch)
    write_vs_launch_json()
endif()

# ---------------------------------------------------------------------------
# Install / package config (find_package(vox4c CONFIG))
# ---------------------------------------------------------------------------
if(VOX4C_INSTALL)
    # the header-only package contains nothing architecture specific; avoids
    # GNUInstallDirs' "no target architecture" warning when no language is enabled
    if(NOT DEFINED CMAKE_SIZEOF_VOID_P AND NOT DEFINED CMAKE_INSTALL_LIBDIR)
        set(CMAKE_INSTALL_LIBDIR lib)
    endif()
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(VOX4C_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/cmake/vox4c" CACHE STRING
        "Install folder of the vox4c CMake package files")

    set(_vox4c_install_targets vox4c)
    if(VOX4C_BUILD_LIBRARY)
        list(APPEND _vox4c_install_targets vox4c_lib)
    endif()
    install(TARGETS ${_vox4c_install_targets} EXPORT vox4cTargets
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/vox4c.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION ${CMAKE_INSTALL_DOCDIR})

    install(EXPORT vox4cTargets
        FILE vox4cTargets.cmake
        NAMESPACE vox4c::
        DESTINATION ${VOX4C_CMAKE_INSTALL_DIR})

    # values baked into vox4cConfig.cmake
    set(VOX4C_CONFIG_HAS_LIBRARY ${VOX4C_BUILD_LIBRARY})
    set(VOX4C_CONFIG_MSVC_RUNTIME "")
    if(VOX4C_BUILD_LIBRARY)
        _vox4c_msvc_abi(_vox4c_msvc)
        if(_vox4c_msvc)
            string(TOUPPER "${VOX4C_MSVC_RUNTIME}" VOX4C_CONFIG_MSVC_RUNTIME)
            if(VOX4C_CONFIG_MSVC_RUNTIME STREQUAL "DEFAULT")
                if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
                    set(VOX4C_CONFIG_MSVC_RUNTIME "MT")
                else()
                    set(VOX4C_CONFIG_MSVC_RUNTIME "MD")
                endif()
            endif()
        endif()
    endif()

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/Vox4cConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/Vox4cConfig.cmake"
        INSTALL_DESTINATION ${VOX4C_CMAKE_INSTALL_DIR})
    if(VOX4C_BUILD_LIBRARY)
        # contains a compiled library: 32 / 64 bit must match
        write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/Vox4cConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)
    else()
        write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/Vox4cConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
            ARCH_INDEPENDENT)
    endif()

    install(FILES
        "${PROJECT_BINARY_DIR}/Vox4cConfig.cmake"
        "${PROJECT_BINARY_DIR}/Vox4cConfigVersion.cmake"
        DESTINATION ${VOX4C_CMAKE_INSTALL_DIR})
endif()

if(VOX4C_IS_TOP_LEVEL)
    message(STATUS "vox4c ${PROJECT_VERSION}: library=${VOX4C_BUILD_LIBRARY} tests=${VOX4C_BUILD_TESTS} "
                   "examples=${VOX4C_BUILD_EXAMPLES} install=${VOX4C_INSTALL} sanitize=${VOX4C_SANITIZE} clang-tidy=${_vox4c_clang_tidy}")
    if(_vox4c_needs_c)
        _vox4c_msvc_abi(_vox4c_msvc)
        if(_vox4c_msvc)
            message(STATUS "vox4c: MSVC runtime ${VOX4C_MSVC_RUNTIME}")
        endif()
    endif()
    if(VOX4C_BUILD_TESTS OR VOX4C_BUILD_EXAMPLES)
        message(STATUS "vox4c: .vox files are taken from ${VOX4C_VOX_FILES_DIR}")
    endif()
endif()