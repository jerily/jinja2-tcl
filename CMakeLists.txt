cmake_minimum_required(VERSION 3.22.1)
project(jinja2 VERSION 0.1.0 LANGUAGES C CXX)

option(BUILD_SHARED_LIBS "Build the loadable Tcl extension" ON)
include(CTest)
include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(TclStub 8.6 REQUIRED)
find_package(CJSON REQUIRED)

# The copied engine is private; cJSON is an external dependency.
add_library(jinja2_engine OBJECT
    src/engine/jinja2.cpp
    src/engine/string_utils.cpp
)
target_compile_features(jinja2_engine PUBLIC cxx_std_17)
set_target_properties(jinja2_engine PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_include_directories(jinja2_engine PUBLIC src/engine)
target_link_libraries(jinja2_engine PUBLIC CJSON::CJSON)

add_library(jinja2 src/library.cpp)
target_link_libraries(jinja2 PRIVATE jinja2_engine)
target_compile_features(jinja2 PUBLIC cxx_std_17)
target_include_directories(jinja2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/jinja2>
    ${TCL_INCLUDE_PATH}
)
target_compile_definitions(jinja2 PRIVATE BUILD_jinja2 TCL_THREADS
    JINJA2_VERSION="${PROJECT_VERSION}")
set_target_properties(jinja2 PROPERTIES
    PREFIX "lib" POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(jinja2 PRIVATE USE_TCL_STUBS)
    target_link_libraries(jinja2 PRIVATE ${TCL_STUB_LIBRARY})
else()
    # The embedding application links Tcl and registers Jinja2_Init.
    target_compile_definitions(jinja2 PUBLIC STATIC_BUILD)
endif()
if(MSVC)
    target_compile_options(jinja2 PRIVATE /W4)
else()
    target_compile_options(jinja2 PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(jinja2_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(JINJA2_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/jinja2${PROJECT_VERSION}")
install(TARGETS jinja2
    LIBRARY DESTINATION ${JINJA2_INSTALL_DIR}
    ARCHIVE DESTINATION ${JINJA2_INSTALL_DIR}
    RUNTIME DESTINATION ${JINJA2_INSTALL_DIR}
)
install(FILES src/library.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jinja2)
if(BUILD_SHARED_LIBS)
    configure_file(pkgIndex.tcl.in pkgIndex.tcl @ONLY)
    if(CMAKE_CONFIGURATION_TYPES)
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/pkgIndex.tcl"
            INPUT "${CMAKE_CURRENT_BINARY_DIR}/pkgIndex.tcl")
    endif()
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pkgIndex.tcl"
        DESTINATION ${JINJA2_INSTALL_DIR})
endif()
install(FILES readme.md install.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES docs/template-language.md docs/provenance.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)

if(BUILD_TESTING)
    add_executable(jinja2_engine_tests tests/test_engine.cpp)
    target_link_libraries(jinja2_engine_tests PRIVATE jinja2_engine)
    add_test(NAME engine COMMAND jinja2_engine_tests)

    if(BUILD_SHARED_LIBS)
        if(NOT TCL_TCLSH)
            message(FATAL_ERROR "Tests require a matching tclsh; set TCL_TCLSH or BUILD_TESTING=OFF")
        endif()
        set(JINJA2_TEST_SHELL "${TCL_TCLSH}")
        set(JINJA2_TEST_PACKAGE_DIR "$<TARGET_FILE_DIR:jinja2>")
    else()
        string(REGEX MATCH "^[0-9]+\\.[0-9]+" TCL_ABI_VERSION "${TCL_VERSION}")
        string(REPLACE "." "" TCL_ABI_COMPACT "${TCL_ABI_VERSION}")
        find_library(TCL_LIBRARY NAMES tcl${TCL_ABI_VERSION} tcl${TCL_ABI_COMPACT} tcl
            HINTS ${TCL_LIBRARY_DIR} "${TCL_INCLUDE_PATH_PARENT}/lib" REQUIRED)
        add_executable(jinja2_test_shell tests/static_shell.cpp)
        target_link_libraries(jinja2_test_shell PRIVATE jinja2 ${TCL_LIBRARY})
        set(JINJA2_TEST_SHELL "$<TARGET_FILE:jinja2_test_shell>")
        set(JINJA2_TEST_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    add_test(NAME tcl COMMAND "${JINJA2_TEST_SHELL}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/all.tcl" "${JINJA2_TEST_PACKAGE_DIR}")
endif()
