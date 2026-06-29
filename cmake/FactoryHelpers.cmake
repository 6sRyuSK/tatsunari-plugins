# factory_read_version(<toml_path> <out_var>)
# Extract  version = "X.Y.Z"  from a plugin.toml so the manifest is the single
# source of truth for the plugin's version (catalog == binary == release).
function(factory_read_version toml_path out_var)
  file(READ "${toml_path}" _toml)
  string(REGEX MATCH "version[ \t]*=[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _m "${_toml}")
  if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "No semantic version found in ${toml_path}")
  endif()
  set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
