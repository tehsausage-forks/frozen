cmake_minimum_required(VERSION 3.9)
project(elsa LANGUAGES C)

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

include_directories(include)

option(ELSA_CHECK_ALL_STANDARDS
  "Checks C90, C99, C11 and C17 compliance" OFF)
option(ELSA_CHECK_COVERAGE
  "Enables code coverage checking (for Debug builds) (clang/gcc only)" OFF)
option(ELSA_CHECK_SIZE
  "Builds a simple program to check binary size (for Release builds)" OFF)

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
    VERSION 1
    SOVERSION 1
    PUBLIC_HEADER include/elsa.h
)

if(ELSA_CHECK_ALL_STANDARDS)
  add_library(elsa90 STATIC $<TARGET_PROPERTY:elsa,SOURCES>)
  set_property(TARGET elsa90 PROPERTY C_STANDARD 90)
  set_property(TARGET elsa90 PROPERTY C_EXTENSIONS ON)

  add_library(elsa99 STATIC $<TARGET_PROPERTY:elsa,SOURCES>)
  set_property(TARGET elsa99 PROPERTY C_STANDARD 99)
  set_property(TARGET elsa99 PROPERTY C_EXTENSIONS OFF)

  add_library(elsa11 STATIC $<TARGET_PROPERTY:elsa,SOURCES>)
  set_property(TARGET elsa11 PROPERTY C_STANDARD 11)
  set_property(TARGET elsa11 PROPERTY C_EXTENSIONS OFF)

  if(("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU"))
    add_library(elsa17 STATIC $<TARGET_PROPERTY:elsa,SOURCES>)
    target_compile_options(elsa17 PRIVATE -std=c17)
  endif()
endif()

if(ELSA_CHECK_SIZE)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "CMAKE_BUILD_TYPE should be Release for a valid size test")
  endif()

  add_executable(size_test size_test.c)
  target_link_libraries(size_test elsa)
  set_property(TARGET size_test PROPERTY C_STANDARD 99)
  set_property(TARGET size_test PROPERTY C_EXTENSIONS OFF)

  add_custom_command(TARGET size_test
    POST_BUILD
    COMMAND strip "$<TARGET_FILE:size_test>"
    COMMAND echo "size_test:" `cat size_test | wc -c` "bytes"
  )
endif()

add_executable(unit_test unit_test.c)
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

include(GNUInstallDirs)

# will install:
# - lib/elsa.a (or lib/elsa.so if BUILD_SHARED_LIBS is set)
# - include/elsa.h
install(TARGETS elsa
  EXPORT elsa-targets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT elsa-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/elsa"
  NAMESPACE elsa
)
