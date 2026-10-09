# Compiler and linker settings of vox4c's own targets.
#
# Everything here is PRIVATE to vox4c's targets (or limited to the build tree),
# nothing is forced onto projects that link vox4c::vox4c or vox4c::lib, apart
# from what is needed to link the static library correctly.

# ---------------------------------------------------------------------------
# MSVC runtime library (/MD or /MT)
#
# All objects linked into one binary must use the same C runtime, otherwise
# the MSVC linker reports LNK2038 / LNK4098. VOX4C_MSVC_RUNTIME:
#   DEFAULT  follow CMAKE_MSVC_RUNTIME_LIBRARY of the including project, i.e.
#            whatever the rest of the build uses (CMake's default is /MD)
#   MD       /MD  (Debug: /MDd), dynamic CRT - the usual choice
#   MT       /MT  (Debug: /MTd), static CRT - for self-contained executables
# The Debug variant is picked per configuration, also with multi-config
# generators (Visual Studio, Ninja Multi-Config).
# ---------------------------------------------------------------------------
set(VOX4C_MSVC_RUNTIME "DEFAULT" CACHE STRING "MSVC C runtime of vox4c's targets: DEFAULT, MD or MT")
set_property(CACHE VOX4C_MSVC_RUNTIME PROPERTY STRINGS DEFAULT MD MT)
string(TOUPPER "${VOX4C_MSVC_RUNTIME}" _vox4c_runtime)
if(_vox4c_runtime STREQUAL "MD")
    set(VOX4C_MSVC_RUNTIME_VALUE "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
elseif(_vox4c_runtime STREQUAL "MT")
    set(VOX4C_MSVC_RUNTIME_VALUE "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(_vox4c_runtime STREQUAL "DEFAULT")
    set(VOX4C_MSVC_RUNTIME_VALUE "")
else()
    message(FATAL_ERROR "VOX4C_MSVC_RUNTIME must be DEFAULT, MD or MT (is '${VOX4C_MSVC_RUNTIME}')")
endif()

# Is a compiler with the MSVC ABI (cl.exe, clang-cl, clang targeting *-windows-msvc) in use?
function(_vox4c_msvc_abi out_var)
    set(_abi OFF)
    if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        set(_abi ON)
    endif()
    set(${out_var} ${_abi} PARENT_SCOPE)
endfunction()

# Applies the runtime choice to one target (C and C++ targets alike).
function(vox4c_set_msvc_runtime target)
    if(VOX4C_MSVC_RUNTIME_VALUE)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "${VOX4C_MSVC_RUNTIME_VALUE}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Warnings (PRIVATE, never propagated)
# ---------------------------------------------------------------------------
function(vox4c_set_warnings target)
    _vox4c_msvc_abi(_msvc)
    if(MSVC)
        # cl.exe and clang-cl
        target_compile_options(${target} PRIVATE /W4)
        if(VOX4C_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        # the library itself is kept free of implicit conversion warnings
        get_target_property(_type ${target} TYPE)
        if(_type STREQUAL "STATIC_LIBRARY")
            target_compile_options(${target} PRIVATE -Wconversion -Wsign-conversion)
        endif()
        if(VOX4C_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    # fopen & co. are fine in portable C, silence the "use fopen_s" advice
    if(_msvc OR MINGW)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Sanitizers (tests / examples, and the library when it is built with them)
# ---------------------------------------------------------------------------
function(vox4c_set_sanitizers target)
    if(NOT VOX4C_SANITIZE)
        return()
    endif()
    if(MSVC)
        # MSVC has ASan only (no UBSan); it is incompatible with /RTC1 of the
        # Debug configuration and with incremental linking
        # (/RTC1 is removed from the Debug flags in the top-level CMakeLists.txt)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        get_target_property(_type ${target} TYPE)
        if(NOT _type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        get_target_property(_type ${target} TYPE)
        if(NOT _type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Settings for the static library vox4c::lib
# ---------------------------------------------------------------------------
function(vox4c_configure_static_library target)
    _vox4c_msvc_abi(_msvc)

    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        # usable in executables, PIE executables and shared libraries (-fPIC)
        POSITION_INDEPENDENT_CODE ON
        # vox4c's symbols stay internal when the library ends up inside a
        # shared library of the user (no clashes between two such libraries);
        # linking and calling it directly is not affected
        C_VISIBILITY_PRESET hidden
        # Debug and Release can be installed side by side (vox4cd / vox4c)
        DEBUG_POSTFIX d
        # no link-time optimisation on purpose: LTO objects in a static library
        # only link with the very same compiler version and LTO-aware tools
        INTERPROCEDURAL_OPTIMIZATION OFF
        FOLDER "vox4c")

    vox4c_set_msvc_runtime(${target})
    vox4c_set_warnings(${target})
    vox4c_set_sanitizers(${target})

    if(_msvc)
        # function / data level linking: unused parts can be dropped by the linker
        target_compile_options(${target} PRIVATE /Gy /Gw)
        # Debug info inside the .obj files (/Z7) instead of a separate PDB,
		# otherwise every user of the installed .lib gets LNK4099 "PDB not found".
        set_property(TARGET ${target} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # same idea for GCC / Clang: lets --gc-sections / -dead_strip remove
        # unused functions from the final binary
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Settings for test / example executables
# ---------------------------------------------------------------------------
function(vox4c_configure_executable target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        FOLDER "vox4c")
    vox4c_set_msvc_runtime(${target})
    vox4c_set_warnings(${target})
    vox4c_set_sanitizers(${target})
    # libm for the tests (fabs / sqrt); vox4c itself does not need it
    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()