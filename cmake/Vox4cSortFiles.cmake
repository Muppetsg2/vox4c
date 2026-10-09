# Ordering of .vox file lists (reports, test runs, Visual Studio launch configurations).
#
#   vox4c_sort_vox_files(<out_var> <root folder> <file>...)
#
# At every folder level the files come first, then the sub-folders; files and
# folders are sorted alphabetically, ignoring case:
#
#   b.vox
#   C.vox
#   anim/deer.vox
#   anim/T-Rex.vox
#   older/castle.vox
#   older/anim/horse.vox
#
# Works in script mode (cmake -P) as well as during configuration.

include_guard(GLOBAL)

function(vox4c_sort_vox_files out_var root)
    set(_keyed "")
    foreach(_file IN LISTS ARGN)
        file(RELATIVE_PATH _rel "${root}" "${_file}")
        string(TOLOWER "${_rel}" _lower)
        # mark every component: folders sort after files of the same level
        string(REPLACE "/" ";" _parts "${_lower}")
        list(LENGTH _parts _count)
        math(EXPR _last "${_count} - 1")
        set(_key "")
        set(_index 0)
        foreach(_part IN LISTS _parts)
            if(_index EQUAL _last)
                string(APPEND _key "0${_part}")          # the file name
            else()
                string(APPEND _key "1${_part}\t")        # a folder; tab sorts before any name character
            endif()
            math(EXPR _index "${_index} + 1")
        endforeach()
        # the original relative path breaks ties between names that differ only in case
        list(APPEND _keyed "${_key}\n${_rel}\n${_file}")
    endforeach()
    list(SORT _keyed)
    set(_sorted "")
    foreach(_entry IN LISTS _keyed)
        string(REGEX REPLACE "^[^\n]*\n[^\n]*\n" "" _file "${_entry}")
        list(APPEND _sorted "${_file}")
    endforeach()
    set(${out_var} "${_sorted}" PARENT_SCOPE)
endfunction()