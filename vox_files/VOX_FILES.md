# vox_files

Put `.vox` files here (sub-folders are fine). They are used by the CMake targets
`vox4c_run_tests`, `vox4c_run_info`, `vox4c_memory_report`, `vox4c_export_obj`
and by the CTest test `vox4c.vox_files`. The folder is searched each time a
target runs, so there is no need to re-run CMake after adding files.

A different folder can be used with `-DVOX4C_VOX_FILES_DIR=/path/to/folder`.