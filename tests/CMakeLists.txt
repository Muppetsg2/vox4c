# ---- C tests, implementation compiled into the test (vox4c::vox4c) --------------
add_executable(vox4c_tests vox4c_tests.c)
target_link_libraries(vox4c_tests PRIVATE vox4c::vox4c)
vox4c_configure_executable(vox4c_tests)
if(_vox4c_clang_tidy)
    vox4c_enable_clang_tidy(vox4c_tests)
endif()

# built-in tests only (argument checks, corrupted data, every MATL combination)
add_test(NAME vox4c.builtin COMMAND vox4c_tests)
# every .vox file of VOX4C_VOX_FILES_DIR, searched when the test runs
vox4c_add_vox_files_test(vox4c.vox_files MODE TEST EXECUTABLE vox4c_tests)

# cmake --build <dir> --target vox4c_run_tests
vox4c_add_vox_files_target(vox4c_run_tests
    MODE TEST
    EXECUTABLE vox4c_tests
    COMMENT "vox4c: built-in tests + every .vox file in ${VOX4C_VOX_FILES_DIR}")

# same, with the full per-file output (scene statistics)
vox4c_add_vox_files_target(vox4c_run_tests_verbose
    MODE TEST
    EXECUTABLE vox4c_tests
    VERBOSE
    COMMENT "vox4c: built-in tests + every .vox file (verbose)")

# ---- the same tests against the static library vox4c::lib ------------------------
if(TARGET vox4c::lib)
    add_executable(vox4c_tests_lib vox4c_tests.c)
    target_link_libraries(vox4c_tests_lib PRIVATE vox4c::lib)
    vox4c_configure_executable(vox4c_tests_lib)
	if(_vox4c_clang_tidy)
        vox4c_enable_clang_tidy(vox4c_tests_lib)
    endif()

    add_test(NAME vox4c.lib.builtin COMMAND vox4c_tests_lib)
    vox4c_add_vox_files_test(vox4c.lib.vox_files MODE TEST EXECUTABLE vox4c_tests_lib)
    vox4c_add_vox_files_target(vox4c_run_tests_lib
        MODE TEST
        EXECUTABLE vox4c_tests_lib
        COMMENT "vox4c: tests against vox4c::lib + every .vox file in ${VOX4C_VOX_FILES_DIR}")
endif()

# ---- misuse check: defining VOX4C_IMPLEMENTATION next to vox4c::lib --------------
# Must stop with the explanatory #error of vox4c.h instead of duplicate symbols.
# The target never compiles, so it is not part of "all" and cannot be run;
# vox4c_run_misuse_check (and CTest) build it and expect that error.
# With vox4c::lib it links the library, which provides VOX4C_PREBUILT; without
# it the definition is set directly, so the check is always available.
add_executable(vox4c_misuse_check EXCLUDE_FROM_ALL vox4c_misuse_check.c)
if(TARGET vox4c::lib)
	target_link_libraries(vox4c_misuse_check PRIVATE vox4c::lib)
else()
    target_link_libraries(vox4c_misuse_check PRIVATE vox4c::vox4c)
    target_compile_definitions(vox4c_misuse_check PRIVATE VOX4C_PREBUILT)
endif()

vox4c_set_msvc_runtime(vox4c_misuse_check)
set(_vox4c_misuse_command "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTARGET=vox4c_misuse_check"
    "-DCONFIG=$<CONFIG>"
    "-DEXPECT=already contains the implementation"
    -P "${PROJECT_SOURCE_DIR}/cmake/vox4cExpectBuildError.cmake")
add_test(NAME vox4c.misuse_is_reported COMMAND ${_vox4c_misuse_command})
# cmake --build <dir> --target vox4c_run_misuse_check
add_custom_target(vox4c_run_misuse_check
    COMMAND ${_vox4c_misuse_command}
    COMMENT "vox4c: checking that VOX4C_IMPLEMENTATION next to vox4c::lib is rejected"
    USES_TERMINAL
    VERBATIM)

# ---- C++: the header compiled as C++, and C++ linking the C library --------------
include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)

    set(_cpp_tests vox4c_cpp_tests)
    add_executable(vox4c_cpp_tests vox4c_cpp_tests.cpp)
    target_link_libraries(vox4c_cpp_tests PRIVATE vox4c::vox4c)
    add_test(NAME vox4c.cpp COMMAND vox4c_cpp_tests)

    if(TARGET vox4c::lib)
        list(APPEND _cpp_tests vox4c_cpp_tests_lib)
        add_executable(vox4c_cpp_tests_lib vox4c_cpp_tests.cpp)
        target_link_libraries(vox4c_cpp_tests_lib PRIVATE vox4c::lib)
        add_test(NAME vox4c.lib.cpp COMMAND vox4c_cpp_tests_lib)
    endif()

    foreach(_t IN LISTS _cpp_tests)
        vox4c_configure_executable(${_t})
        set_target_properties(${_t} PROPERTIES
            CXX_STANDARD 11
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF)
		if(_vox4c_clang_tidy)
            vox4c_enable_clang_tidy(${_t})
        endif()
    endforeach()
else()
    message(STATUS "vox4c: no C++ compiler, skipping the C++ tests")
endif()

# ---- Visual Studio debug configurations ---------------------------------------------
if(_vox4c_vs_launch)
    add_vs_launch_config(NAME "vox4c_tests (built-in tests)" TARGET vox4c_tests)
    if(VOX4C_VS_LAUNCH_FILES)
        add_vs_launch_config(NAME "vox4c_tests (vox_files)" TARGET vox4c_tests ARGS ${VOX4C_VS_LAUNCH_FILES})
    endif()
    if(TARGET vox4c_tests_lib)
        add_vs_launch_config(NAME "vox4c_tests_lib (built-in tests)" TARGET vox4c_tests_lib)
        if(VOX4C_VS_LAUNCH_FILES)
            add_vs_launch_config(NAME "vox4c_tests_lib (vox_files)" TARGET vox4c_tests_lib ARGS ${VOX4C_VS_LAUNCH_FILES})
        endif()
    endif()
    foreach(_t vox4c_cpp_tests vox4c_cpp_tests_lib)
        if(TARGET ${_t})
            add_vs_launch_config(NAME "${_t}" TARGET ${_t})
        endif()
    endforeach()
endif()