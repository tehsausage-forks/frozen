cmake_minimum_required(VERSION 3.9)
project(frozen-gplv2 LANGUAGES C)

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

include_directories(include)

option(FROZEN_CHECK_ALL_STANDARDS
  "Checks C90, C99, C11 and C17 compliance" OFF)
option(FROZEN_CHECK_COVERAGE
  "Enables code coverage checking (for Debug builds) (clang/gcc only)" OFF)
option(FROZEN_CHECK_SIZE
  "Builds a simple program to check binary size (for Release builds)" OFF)

add_library(frozen
  include/frozen.h
  frozen/escape.c
  frozen/fread.c
  frozen/next.c
  frozen/prettify.c
  frozen/printer.c
  frozen/printf.c
  frozen/scanf.c
  frozen/setf.c
  frozen/util.h
  frozen/walk.c
)

target_include_directories(frozen
  PUBLIC
    "$<INSTALL_INTERFACE:include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

set_target_properties(frozen
  PROPERTIES
    VERSION 1
    SOVERSION 1
    PUBLIC_HEADER include/frozen.h
)

if(FROZEN_CHECK_ALL_STANDARDS)
  add_library(frozen90 STATIC $<TARGET_PROPERTY:frozen,SOURCES>)
  set_property(TARGET frozen90 PROPERTY C_STANDARD 90)
  set_property(TARGET frozen90 PROPERTY C_EXTENSIONS ON)

  add_library(frozen99 STATIC $<TARGET_PROPERTY:frozen,SOURCES>)
  set_property(TARGET frozen99 PROPERTY C_STANDARD 99)
  set_property(TARGET frozen99 PROPERTY C_EXTENSIONS OFF)

  add_library(frozen11 STATIC $<TARGET_PROPERTY:frozen,SOURCES>)
  set_property(TARGET frozen11 PROPERTY C_STANDARD 11)
  set_property(TARGET frozen11 PROPERTY C_EXTENSIONS OFF)

  if(("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU"))
    add_library(frozen17 STATIC $<TARGET_PROPERTY:frozen,SOURCES>)
    target_compile_options(frozen17 PRIVATE -std=c17)
  endif()
endif()

if(FROZEN_CHECK_SIZE)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "CMAKE_BUILD_TYPE should be Release for a valid size test")
  endif()

  add_executable(size_test size_test.c)
  target_link_libraries(size_test frozen)
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

if(FROZEN_CHECK_COVERAGE)
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
# - lib/frozen.a (or lib/frozen.so if BUILD_SHARED_LIBS is set)
# - include/frozen.h
install(TARGETS frozen
  EXPORT frozen-targets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT frozen-targets
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/frozen"
  NAMESPACE frozen
)
