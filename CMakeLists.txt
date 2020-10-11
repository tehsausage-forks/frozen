cmake_minimum_required(VERSION 3.9)
project(elsa VERSION 1.0 LANGUAGES C)

# ----------

option(ELSA_CHECK_COVERAGE
  "Enables code coverage checking (for Debug builds) (clang/gcc only)" OFF)

# ----------

add_library(elsa
  include/elsa.h
  elsa/escape.c
  elsa/fread.c
  elsa/next.c
  elsa/prettify.c
  elsa/printer.c
  elsa/printf.c
  elsa/scanf.c
  elsa/setf.c
  elsa/util.h
  elsa/walk.c
)

target_include_directories(elsa
  PUBLIC
    "$<INSTALL_INTERFACE:include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

set_target_properties(elsa
  PROPERTIES
    SOVERSION 1
    PUBLIC_HEADER include/elsa.h
)

# ----------

add_executable(unit_test unit_test.c)
target_include_directories(unit_test PRIVATE include)

set_property(TARGET unit_test PROPERTY C_STANDARD 99)
set_property(TARGET unit_test PROPERTY C_EXTENSIONS OFF)

if(ELSA_CHECK_COVERAGE)
  if(CMAKE_BUILD_TYPE MATCHES "Rel")
    message(WARNING "CMAKE_BUILD_TYPE should be Debug for code coverage")
  endif()

  # clang has gcc compatible code coverage analysys
  # use `llvm-cov gcov` in place of `gcov` to process gcda
  if(("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU"))
    target_compile_options(unit_test PRIVATE
      -fprofile-arcs
      -ftest-coverage
    )
    target_link_options(unit_test PRIVATE
      -fprofile-arcs
      -ftest-coverage
    )
    target_link_libraries(unit_test gcov)
    add_custom_command(TARGET unit_test
      POST_BUILD
      COMMAND cmake -E rm -f
        "$<TARGET_FILE_DIR:unit_test>/CMakeFiles/unit_test.dir/unit_test.c.gcda"
    )
  else()
    message(WARNING "code coverage not available for ${CMAKE_C_COMPILER_ID}")
  endif()
endif()

# ----------

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

configure_file(elsa.pc.in elsa.pc @ONLY)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/elsa-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

# will install:
# - bin/elsa.dll (for windows if BUILD_SHARED_LIBS is set)
# - lib/libelsa.a (or lib/libelsa.so if BUILD_SHARED_LIBS is set)
# - include/elsa.h
# - lib/pkgconfig/elsa.pc
# - lib/cmake/elsa/elsa-{config,config-version,targets}.cmake

install(TARGETS elsa
  EXPORT elsa-targets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT elsa-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/elsa"
)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/elsa-config.cmake
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/elsa-targets.cmake\")"
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/elsa-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/elsa-config-version.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/elsa"
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/elsa.pc
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
