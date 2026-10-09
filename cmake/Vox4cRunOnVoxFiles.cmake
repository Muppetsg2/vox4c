# Runs a vox4c executable on every *.vox file found (recursively) in a folder.
# Invoked in script mode by the vox4c_* custom targets and by CTest, so the
# folder is searched at run time: new files are picked up without re-running
# CMake.
#
#   cmake -DVOX4C_MODE=<TEST|EACH|MEMORY|OBJ>
#         -DVOX4C_EXE=<path to executable>
#         -DVOX4C_VOX_DIR=<folder with .vox files>
#         [-DVOX4C_ARGS=<extra arguments, separated with |>]
#         [-DVOX4C_OUTPUT=<output file (MEMORY) or folder (OBJ)>]
#         [-DVOX4C_INJECT=<markdown file> -DVOX4C_INJECT_TAG=<tag>]   (MEMORY)
#         [-DVOX4C_TIMEOUT=<seconds per file>]
#         [-DVOX4C_VERBOSE=ON]
#         -P vox4cRunOnVoxFiles.cmake
#
# Modes
#   TEST    run the executable once without files (built-in tests), then once
#           per file; prints [ OK ] / [FAIL] per file and fails if any run fails
#   EACH    run "<exe> <file> <args>" for every file, output shown as is
#   MEMORY  run "<exe> <args> <file>" for every file (vox4c_memory) and merge
#           the Markdown rows into one table, printed and written to OUTPUT;
#           with INJECT the table also replaces the text between the lines
#             <!-- vox4c:<tag>:begin -->  and  <!-- vox4c:<tag>:end -->
#           of that Markdown file (invisible when rendered)
#   OBJ     run "<exe> <file> <OUTPUT>/<relative path>.obj <args>" (vox2obj),
#           mirroring the sub-folders of VOX_DIR

cmake_minimum_required(VERSION 3.25)

foreach(_required VOX4C_MODE VOX4C_EXE VOX4C_VOX_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "vox4cRunOnVoxFiles: ${_required} is not set")
    endif()
endforeach()

set(_args "")
if(DEFINED VOX4C_ARGS AND NOT VOX4C_ARGS STREQUAL "")
    string(REPLACE "|" ";" _args "${VOX4C_ARGS}")
endif()
set(_timeout_args "")
if(DEFINED VOX4C_TIMEOUT AND NOT VOX4C_TIMEOUT STREQUAL "")
    set(_timeout_args TIMEOUT ${VOX4C_TIMEOUT})
endif()

# ---- collect files ---------------------------------------------------------
set(_files "")
if(IS_DIRECTORY "${VOX4C_VOX_DIR}")
    file(GLOB_RECURSE _files LIST_DIRECTORIES false
        "${VOX4C_VOX_DIR}/*.vox" "${VOX4C_VOX_DIR}/*.VOX" "${VOX4C_VOX_DIR}/*.Vox")
    list(REMOVE_DUPLICATES _files)
    # files of a folder first, then its sub-folders, alphabetically
    include("${CMAKE_CURRENT_LIST_DIR}/Vox4cSortFiles.cmake")
    vox4c_sort_vox_files(_files "${VOX4C_VOX_DIR}" ${_files})
else()
    message(WARNING "vox4c: folder '${VOX4C_VOX_DIR}' does not exist")
endif()
list(LENGTH _files _count)
message(STATUS "vox4c: ${_count} .vox file(s) in ${VOX4C_VOX_DIR}")

set(_failed 0)
set(_failed_files "")

macro(_vox4c_record_failure file_rel)
    math(EXPR _failed "${_failed} + 1")
    list(APPEND _failed_files "${file_rel}")
endmacro()

# ---- TEST -----------------------------------------------------------------
if(VOX4C_MODE STREQUAL "TEST")
    execute_process(COMMAND "${VOX4C_EXE}" ${_args}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out ${_timeout_args})
    if(_rc EQUAL 0)
        message(STATUS "[ OK ] built-in tests")
        if(VOX4C_VERBOSE)
            message("${_out}")
        endif()
    else()
        message("[FAIL] built-in tests (${_rc})\n${_out}")
        _vox4c_record_failure("<built-in tests>")
    endif()

    foreach(_file IN LISTS _files)
        file(RELATIVE_PATH _rel "${VOX4C_VOX_DIR}" "${_file}")
        execute_process(COMMAND "${VOX4C_EXE}" ${_args} "${_file}"
            RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out ${_timeout_args})
        if(_rc EQUAL 0)
            message(STATUS "[ OK ] ${_rel}")
            if(VOX4C_VERBOSE)
                message("${_out}")
            endif()
        else()
            message("[FAIL] ${_rel} (${_rc})\n${_out}")
            _vox4c_record_failure("${_rel}")
        endif()
    endforeach()

# ---- EACH -----------------------------------------------------------------
elseif(VOX4C_MODE STREQUAL "EACH")
    foreach(_file IN LISTS _files)
        file(RELATIVE_PATH _rel "${VOX4C_VOX_DIR}" "${_file}")
        message("==== ${_rel}")
        execute_process(COMMAND "${VOX4C_EXE}" "${_file}" ${_args} RESULT_VARIABLE _rc ${_timeout_args})
        if(NOT _rc EQUAL 0)
            message("[FAIL] ${_rel} (${_rc})")
            _vox4c_record_failure("${_rel}")
        endif()
    endforeach()

# ---- MEMORY ---------------------------------------------------------------
elseif(VOX4C_MODE STREQUAL "MEMORY")
    set(_header_line "")
    set(_separator_line "")
    set(_rows "")
    foreach(_file IN LISTS _files)
        file(RELATIVE_PATH _rel "${VOX4C_VOX_DIR}" "${_file}")
        execute_process(COMMAND "${VOX4C_EXE}" ${_args} "${_file}"
            RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err ${_timeout_args})
        if(NOT _rc EQUAL 0)
            message("[FAIL] ${_rel} (${_rc}) ${_err}")
            _vox4c_record_failure("${_rel}")
            continue()
        endif()
        string(REPLACE "\n" ";" _lines "${_out}")
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "^\\| file \\|")
                set(_header_line "${_line}")         # same for every file, keep one
            elseif(_line MATCHES "^\\|---")
                set(_separator_line "${_line}")
            elseif(_line MATCHES "^\\|")
                # the tool prints the bare file name, show the path inside VOX_DIR instead
                string(REGEX REPLACE "^\\| [^|]+ \\|" "| ${_rel} |" _line "${_line}")
                string(APPEND _rows "${_line}\n")
            endif()
        endforeach()
    endforeach()
    set(_table "${_header_line}\n${_separator_line}\n${_rows}")
    message("${_table}")
    if(DEFINED VOX4C_OUTPUT AND NOT VOX4C_OUTPUT STREQUAL "")
        file(WRITE "${VOX4C_OUTPUT}" "${_table}")
        message(STATUS "vox4c: table written to ${VOX4C_OUTPUT}")
    endif()

    if(DEFINED VOX4C_INJECT AND NOT VOX4C_INJECT STREQUAL "")
        if(_rows STREQUAL "")
            # never replace an existing table with an empty one
            message(WARNING "vox4c: no table rows, ${VOX4C_INJECT} is left unchanged")
        else()
            if(NOT DEFINED VOX4C_INJECT_TAG OR VOX4C_INJECT_TAG STREQUAL "")
                message(FATAL_ERROR "vox4cRunOnVoxFiles: INJECT needs VOX4C_INJECT_TAG")
            endif()
            set(_begin "<!-- vox4c:${VOX4C_INJECT_TAG}:begin -->")
            set(_end "<!-- vox4c:${VOX4C_INJECT_TAG}:end -->")
            file(READ "${VOX4C_INJECT}" _doc)
            string(FIND "${_doc}" "${_begin}" _b)
            string(FIND "${_doc}" "${_end}" _e)
            if(_b EQUAL -1 OR _e EQUAL -1 OR _e LESS _b)
                message(FATAL_ERROR "vox4c: ${VOX4C_INJECT} has no '${_begin}' ... '${_end}' block")
            endif()
            # keep the file's line endings (CRLF on Windows checkouts)
            set(_nl "\n")
            string(FIND "${_doc}" "\r\n" _crlf)
            if(NOT _crlf EQUAL -1)
                set(_nl "\r\n")
            endif()
            string(REPLACE "\n" "${_nl}" _block "${_table}")
            string(LENGTH "${_begin}" _blen)
            math(EXPR _head_len "${_b} + ${_blen}")
            string(SUBSTRING "${_doc}" 0 ${_head_len} _head)
            string(SUBSTRING "${_doc}" ${_e} -1 _tail)
            set(_new "${_head}${_nl}${_block}${_tail}")
            if(_new STREQUAL _doc)
                message(STATUS "vox4c: ${VOX4C_INJECT} is up to date")
            else()
                file(WRITE "${VOX4C_INJECT}" "${_new}")
                message(STATUS "vox4c: table updated in ${VOX4C_INJECT}")
            endif()
        endif()
    endif()

# ---- OBJ ------------------------------------------------------------------
elseif(VOX4C_MODE STREQUAL "OBJ")
    if(NOT DEFINED VOX4C_OUTPUT OR VOX4C_OUTPUT STREQUAL "")
        message(FATAL_ERROR "vox4cRunOnVoxFiles: OBJ mode needs VOX4C_OUTPUT (folder)")
    endif()
    foreach(_file IN LISTS _files)
        file(RELATIVE_PATH _rel "${VOX4C_VOX_DIR}" "${_file}")
        string(REGEX REPLACE "\\.[vV][oO][xX]$" ".obj" _obj_rel "${_rel}")
        set(_obj "${VOX4C_OUTPUT}/${_obj_rel}")
        get_filename_component(_obj_dir "${_obj}" DIRECTORY)
        file(MAKE_DIRECTORY "${_obj_dir}")
        execute_process(COMMAND "${VOX4C_EXE}" "${_file}" "${_obj}" ${_args} RESULT_VARIABLE _rc ${_timeout_args})
        if(NOT _rc EQUAL 0)
            message("[FAIL] ${_rel} (${_rc})")
            _vox4c_record_failure("${_rel}")
        endif()
    endforeach()
    message(STATUS "vox4c: OBJ files written to ${VOX4C_OUTPUT}")

else()
    message(FATAL_ERROR "vox4cRunOnVoxFiles: unknown mode '${VOX4C_MODE}'")
endif()

# ---- summary --------------------------------------------------------------
if(_failed GREATER 0)
    string(REPLACE ";" "\n  " _list "${_failed_files}")
    message(FATAL_ERROR "vox4c: ${_failed} run(s) failed:\n  ${_list}")
endif()
message(STATUS "vox4c: all ${_count} file(s) processed successfully")