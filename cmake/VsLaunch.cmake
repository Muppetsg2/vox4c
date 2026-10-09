# ---------------------------------------------------------------------------
# Visual Studio launch.vs.json generator
#
# For Visual Studio's "Open Folder" CMake mode: the configurations appear in
# the Startup Item dropdown with their arguments and working directory.
#
#   add_vs_launch_config(NAME <name> TARGET <target> [CWD <dir>] [ARGS <arg>...])
#   ...
#   write_vs_launch_json()          # once, at the end of the top-level CMakeLists.txt
#
# Changes against the original script:
#   * the default working directory is written as the Visual Studio macro
#     ${debugInfo.defaultWorkingDirectory}; unescaped, CMake expanded it as its
#     own (undefined) variable and wrote "cwd": "".
#   * "projectTarget" must match the Startup Item name, "<name>.exe (<path>)",
#     where <path> is the executable relative to the build folder. It is now
#     taken from the target's output folder instead of assuming that every
#     target lives in a folder named after it ("<target>\<target>.exe").
# ---------------------------------------------------------------------------

function(add_vs_launch_config)
    set(options)
    set(oneValueArgs NAME TARGET CWD)
    set(multiValueArgs ARGS)
    cmake_parse_arguments(
        VS_LAUNCH
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    if(NOT VS_LAUNCH_NAME)
        message(FATAL_ERROR
            "add_vs_launch_config: NAME is required"
        )
    endif()

    if(NOT VS_LAUNCH_TARGET)
        message(FATAL_ERROR
            "add_vs_launch_config: TARGET is required"
        )
    endif()

    # Default working directory (a Visual Studio macro, kept literally)
    if(NOT VS_LAUNCH_CWD)
        set(VS_LAUNCH_CWD
            "\${debugInfo.defaultWorkingDirectory}"
        )
    endif()
	
	# Executable path relative to the build folder, as shown by Visual Studio
    set(target_path "${VS_LAUNCH_TARGET}\\${VS_LAUNCH_TARGET}.exe")
    if(TARGET ${VS_LAUNCH_TARGET})
        get_target_property(output_name ${VS_LAUNCH_TARGET} OUTPUT_NAME)
        if(NOT output_name)
            set(output_name ${VS_LAUNCH_TARGET})
        endif()
        get_target_property(output_dir ${VS_LAUNCH_TARGET} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT output_dir)
            get_target_property(output_dir ${VS_LAUNCH_TARGET} BINARY_DIR)
        endif()
        file(RELATIVE_PATH relative_dir "${CMAKE_BINARY_DIR}" "${output_dir}")
        if(relative_dir)
            set(target_path "${relative_dir}/${output_name}.exe")
        else()
            set(target_path "${output_name}.exe")
        endif()
        string(REPLACE "/" "\\" target_path "${target_path}")
        set(exe_name "${output_name}.exe")
    else()
        set(exe_name "${VS_LAUNCH_TARGET}.exe")
    endif()

    # Escape JSON strings
    set(name "${VS_LAUNCH_NAME}")
    string(REPLACE "\\" "\\\\" name "${name}")
    string(REPLACE "\"" "\\\"" name "${name}")

    set(project_target "${exe_name} (${target_path})")
    string(REPLACE "\\" "\\\\" project_target "${project_target}")
    string(REPLACE "\"" "\\\"" project_target "${project_target}")

    set(cwd "${VS_LAUNCH_CWD}")
    string(REPLACE "\\" "\\\\" cwd "${cwd}")
    string(REPLACE "\"" "\\\"" cwd "${cwd}")

    # Generate args
    set(json_args "")

    foreach(arg IN LISTS VS_LAUNCH_ARGS)
        set(escaped_arg "${arg}")

        string(REPLACE "\\" "\\\\" escaped_arg "${escaped_arg}")
        string(REPLACE "\"" "\\\"" escaped_arg "${escaped_arg}")
        string(REPLACE "\n" "\\n" escaped_arg "${escaped_arg}")
        string(REPLACE "\r" "\\r" escaped_arg "${escaped_arg}")

        if(json_args)
            string(APPEND json_args ",\n")
        endif()

        string(APPEND json_args
            "                \"${escaped_arg}\""
        )
    endforeach()

    set(config
"        {
            \"type\": \"default\",
            \"project\": \"CMakeLists.txt\",
            \"projectTarget\": \"${project_target}\",
            \"name\": \"${name}\",
            \"args\": [
${json_args}
            ],
            \"cwd\": \"${cwd}\",
            \"noDebug\": false
        }"
    )

    get_property(configs
        GLOBAL
        PROPERTY VS_LAUNCH_CONFIGS
    )

    if(configs)
        list(APPEND configs "${config}")
    else()
        set(configs "${config}")
    endif()

    set_property(
        GLOBAL
        PROPERTY VS_LAUNCH_CONFIGS
        "${configs}"
    )
endfunction()


function(write_vs_launch_json)
    set(output_file
        "${CMAKE_SOURCE_DIR}/.vs/launch.vs.json"
    )

    file(MAKE_DIRECTORY
        "${CMAKE_SOURCE_DIR}/.vs"
    )

    get_property(configs
        GLOBAL
        PROPERTY VS_LAUNCH_CONFIGS
    )

    if(NOT configs)
        message(WARNING
            "No Visual Studio launch configurations registered"
        )
        return()
    endif()

    set(json_configs "")

    foreach(config IN LISTS configs)
        if(json_configs)
            string(APPEND json_configs ",\n")
        endif()

        string(APPEND json_configs "${config}")
    endforeach()

    set(content
"{\n\
    \"version\": \"0.2.1\",\n\
    \"defaults\": {},\n\
    \"configurations\": [\n\
${json_configs}\n\
    ]\n\
}\n"
    )

    file(GENERATE
        OUTPUT "${output_file}"
        CONTENT "${content}"
    )


    message(STATUS
        "Generated Visual Studio launch configuration: ${output_file}"
    )
endfunction()