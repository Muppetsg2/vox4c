foreach(_example vox4c_info vox2obj vox4c_memory)
    add_executable(${_example} ${_example}.c)
    target_link_libraries(${_example} PRIVATE vox4c::vox4c)
    vox4c_configure_executable(${_example})
	if(_vox4c_clang_tidy)
        vox4c_enable_clang_tidy(${_example})
    endif()
endforeach()

# ---- targets running the examples on every .vox file of VOX4C_VOX_FILES_DIR ----

# cmake --build <dir> --target vox4c_run_info
vox4c_add_vox_files_target(vox4c_run_info
    MODE EACH
    EXECUTABLE vox4c_info
    ARGS --compact
    COMMENT "vox4c: scene / material / node dump of every .vox file")

# cmake --build <dir> --target vox4c_memory_report   (-> <build>/vox4c_memory.md)
vox4c_add_vox_files_target(vox4c_memory_report
    MODE MEMORY
    EXECUTABLE vox4c_memory
    OUTPUT "${CMAKE_BINARY_DIR}/vox4c_memory.md"
    COMMENT "vox4c: mesh memory, float vs packed vertices")
	
# cmake --build <dir> --target vox4c_memory_readme   (-> table in README.md)
# Replaces the table between <!-- vox4c:memory-table:begin/end --> in the
# README with the measurement of the files in VOX4C_VOX_FILES_DIR.
vox4c_add_vox_files_target(vox4c_memory_readme
    MODE MEMORY
    EXECUTABLE vox4c_memory
    OUTPUT "${CMAKE_BINARY_DIR}/vox4c_memory.md"
    INJECT "${PROJECT_SOURCE_DIR}/README.md"
    INJECT_TAG memory-table
    COMMENT "vox4c: updating the memory table in README.md")

vox4c_add_vox_files_target(vox4c_memory_report_compact
    MODE MEMORY
    EXECUTABLE vox4c_memory
    ARGS --compact
    OUTPUT "${CMAKE_BINARY_DIR}/vox4c_memory_compact.md"
    COMMENT "vox4c: mesh memory, float vs packed vertices (used materials only)")

# cmake --build <dir> --target vox4c_export_obj      (-> <build>/obj/...)
vox4c_add_vox_files_target(vox4c_export_obj
    MODE OBJ
    EXECUTABLE vox2obj
    OUTPUT "${CMAKE_BINARY_DIR}/obj"
    COMMENT "vox4c: converting every .vox file to OBJ + MTL")

# ---- GLSL example shaders ----
set(_shaders
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vox4c_packed.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vox4c_packed.frag")
find_program(VOX4C_GLSLANG_VALIDATOR glslangValidator)
if(VOX4C_GLSLANG_VALIDATOR)
    # cmake --build <dir> --target vox4c_validate_shaders
	set(_lens_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vox4c_lens.frag")
    add_custom_target(vox4c_validate_shaders
        COMMAND "${VOX4C_GLSLANG_VALIDATOR}" -l ${_shaders}
		COMMAND "${VOX4C_GLSLANG_VALIDATOR}" "${_lens_shader}"
        COMMENT "vox4c: compiling and linking the example GLSL shaders"
        VERBATIM)
    if(VOX4C_BUILD_TESTS)
        add_test(NAME vox4c.shaders COMMAND "${VOX4C_GLSLANG_VALIDATOR}" -l ${_shaders})
		add_test(NAME vox4c.shaders.lens COMMAND "${VOX4C_GLSLANG_VALIDATOR}" "${_lens_shader}")
    endif()
endif()

# ---- Visual Studio debug configurations ---------------------------------------------
# The examples need a .vox file: the first one in VOX4C_VOX_FILES_DIR is used.
if(_vox4c_vs_launch)
    if(VOX4C_VS_LAUNCH_FILES)
        list(GET VOX4C_VS_LAUNCH_FILES 0 _vox)
        get_filename_component(_vox_name "${_vox}" NAME)
        get_filename_component(_vox_stem "${_vox}" NAME_WE)
        add_vs_launch_config(NAME "vox4c_info (${_vox_name})" TARGET vox4c_info ARGS "${_vox}" --compact)
        add_vs_launch_config(NAME "vox2obj (${_vox_name})" TARGET vox2obj
                             ARGS "${_vox}" "${CMAKE_BINARY_DIR}/${_vox_stem}.obj")
        add_vs_launch_config(NAME "vox4c_memory (vox_files)" TARGET vox4c_memory ARGS ${VOX4C_VS_LAUNCH_FILES})
    else()
        message(STATUS "vox4c: no .vox files in ${VOX4C_VOX_FILES_DIR}, no debug configurations for the examples")
    endif()
endif()