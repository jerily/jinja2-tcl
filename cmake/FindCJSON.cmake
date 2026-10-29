# Find an installed, upstream-compatible cJSON library. Never download or vendor it.
# Accept upstream's CMake package first, then a conventional include/library prefix.
# Override with CJSON_INCLUDE_DIR and CJSON_LIBRARY when necessary.
find_package(cJSON CONFIG QUIET)
if(TARGET cjson)
    set(CJSON_UPSTREAM_TARGET cjson)
elseif(TARGET cJSON::cJSON)
    set(CJSON_UPSTREAM_TARGET cJSON::cJSON)
endif()

if(CJSON_UPSTREAM_TARGET)
    get_target_property(CJSON_INCLUDE_DIRS ${CJSON_UPSTREAM_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    set(CJSON_FORWARD_INCLUDE_DIRS "")
    if(CJSON_INCLUDE_DIRS)
        foreach(directory IN LISTS CJSON_INCLUDE_DIRS)
            if(EXISTS "${directory}/cjson/cJSON.h")
                list(APPEND CJSON_FORWARD_INCLUDE_DIRS "${directory}/cjson")
            endif()
        endforeach()
    endif()
    if(NOT TARGET CJSON::CJSON)
        add_library(CJSON::CJSON INTERFACE IMPORTED)
        set_target_properties(CJSON::CJSON PROPERTIES
            INTERFACE_LINK_LIBRARIES "${CJSON_UPSTREAM_TARGET}"
            INTERFACE_INCLUDE_DIRECTORIES "${CJSON_FORWARD_INCLUDE_DIRS}")
    endif()
    set(CJSON_FOUND TRUE)
else()
    find_path(CJSON_INCLUDE_DIR NAMES cJSON.h PATH_SUFFIXES cjson)
    find_library(CJSON_LIBRARY NAMES cjson)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(CJSON REQUIRED_VARS CJSON_LIBRARY CJSON_INCLUDE_DIR)
    if(CJSON_FOUND AND NOT TARGET CJSON::CJSON)
        add_library(CJSON::CJSON UNKNOWN IMPORTED)
        set_target_properties(CJSON::CJSON PROPERTIES
            IMPORTED_LOCATION "${CJSON_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CJSON_INCLUDE_DIR}")
    endif()
    mark_as_advanced(CJSON_INCLUDE_DIR CJSON_LIBRARY)
endif()
