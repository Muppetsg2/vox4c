# clang-tidy integration of vox4c, built on cmake/clang_tidy.cmake and
# cmake/run_clang_tidy.cmake.
#
# Enabled with -DVOX4C_ENABLE_CLANG_TIDY=ON (only when vox4c is the top-level
# project). For every checked target T this gives
#
#   clang_tidy_T        check the sources of T that changed since the last
#                       clean run (cache in <build>/.../clang-tidy-cache)
#   clang_tidy_T_full   check all sources of T
#
# and, for all of them together,
#
#   vox4c_clang_tidy        / vox4c_clang_tidy_full
#
# The library code is inside vox4c.h; it is checked through the translation
# units that define VOX4C_IMPLEMENTATION (src/vox4c.c, the tests, the
# examples), with the header filter set to vox4c.h.

include_guard(GLOBAL)

set(VOX4C_CLANG_TIDY_CONFIG "${PROJECT_SOURCE_DIR}/.clang-tidy" CACHE FILEPATH "clang-tidy configuration used by vox4c")

include(${CMAKE_CURRENT_LIST_DIR}/ClangTidy.cmake)

# vox4c_enable_clang_tidy(<target> [EXCLUDE <file>...])
# Call it from the directory that created <target>: the runner resolves the
# target's relative source paths from there. C++ targets are checked as C++11
# (their own sources only), all others as C99 together with vox4c.h.
function(vox4c_enable_clang_tidy target)
    cmake_parse_arguments(T "" "" "EXCLUDE" ${ARGN})
    foreach(_file IN LISTS T_EXCLUDE)
        disable_clang_tidy_for_file("${_file}")
    endforeach()
    get_target_property(_sources ${target} SOURCES)
    set(_standard C_STANDARD c99)
    set(_header_filter ".*vox4c.h")
    foreach(_src IN LISTS _sources)
        if(_src MATCHES "\\.(cpp|cxx|cc)$")
            # C++ targets: only their own code. vox4c.h is C and is checked by
            # the C targets; compiled as C++ it only collects C++-only findings
            # (reserved "__" names, bool vs int, const locals).
            set(_standard CXX_STANDARD c++11)
            set(_header_filter "vox4c-no-headers")   # matches no path ("$" would break Ninja)
        endif()
    endforeach()
    enable_clang_tidy(
        TARGET ${target}
        CONFIG "${VOX4C_CLANG_TIDY_CONFIG}"
        HEADER_FILTER "${_header_filter}"
        ${_standard})
    if(TARGET clang_tidy_${target})
        set_property(GLOBAL APPEND PROPERTY VOX4C_CLANG_TIDY_TARGETS ${target})
    endif()
endfunction()

# Creates vox4c_clang_tidy / vox4c_clang_tidy_full, which run the clang_tidy_<target>
# targets of every checked target (in parallel; each run writes its fixes file
# next to its own cache file, so runs do not interfere).
function(vox4c_finalize_clang_tidy)
    get_property(_targets GLOBAL PROPERTY VOX4C_CLANG_TIDY_TARGETS)
    if(NOT _targets)
        return()
    endif()
    foreach(_suffix "" "_full")
        add_custom_target(vox4c_clang_tidy${_suffix})
        foreach(_t IN LISTS _targets)
            add_dependencies(vox4c_clang_tidy${_suffix} clang_tidy_${_t}${_suffix})
        endforeach()
    endforeach()
    string(REPLACE ";" ", " _list "${_targets}")
    message(STATUS "vox4c: clang-tidy for ${_list} (vox4c_clang_tidy, vox4c_clang_tidy_full)")
endfunction()