# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

cmake_minimum_required (VERSION 3.22.1)
project(cswp)

find_package(PkgConfig REQUIRED)
find_package(Doxygen)

enable_testing()

if (Doxygen_FOUND)
  get_property(DOX_INCL_RAW DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
  string(REGEX MATCHALL ";${CMAKE_CURRENT_SOURCE_DIR}[^;]*" DOX_INCL_FILT ";${DOX_INCL_RAW}")
  string(REPLACE ";" " " DOX_INCL "${DOX_INCL_FILT}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
endif()

set(INSTALLED_FILES "")

macro(release_library)
  foreach(lib ${ARGN})
    install(TARGETS ${lib}
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib
    )
    list(APPEND INSTALLED_FILES "lib/$<TARGET_FILE_NAME:${lib}>")
  endforeach()
  set(INSTALLED_FILES ${INSTALLED_FILES} PARENT_SCOPE)
endmacro()

macro(release_program)
  foreach(prog ${ARGN})
    install(TARGETS ${prog}
      RUNTIME DESTINATION bin
    )
    list(APPEND INSTALLED_FILES "bin/$<TARGET_FILE_NAME:${prog}>")
  endforeach()
  set(INSTALLED_FILES ${INSTALLED_FILES} PARENT_SCOPE)
endmacro()

macro(release_file)
  foreach(name ${ARGN})
    install(FILES ${name}
      DESTINATION share/${PROJECT_NAME}
    )
    cmake_path(GET name FILENAME filename)
    list(APPEND INSTALLED_FILES "share/${PROJECT_NAME}/${filename}")
  endforeach()
  set(INSTALLED_FILES ${INSTALLED_FILES} PARENT_SCOPE)
endmacro()

macro(release_script)
  foreach(name ${ARGN})
    install(FILES ${name}
      DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    )
    cmake_path(GET name FILENAME filename)
    list(APPEND INSTALLED_FILES "bin/${filename}")
  endforeach()
  set(INSTALLED_FILES ${INSTALLED_FILES} PARENT_SCOPE)
endmacro()

macro(release_header)
  foreach(name ${ARGN})
    install(FILES ${name}
      DESTINATION ${CMAKE_INSTALL_PREFIX}/include
    )
    cmake_path(GET name FILENAME filename)
    list(APPEND INSTALLED_FILES "include/${filename}")
  endforeach()
  set(INSTALLED_FILES ${INSTALLED_FILES} PARENT_SCOPE)
endmacro()

add_compile_options("$<$<COMPILE_LANGUAGE:C>:-std=c11>")
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

message(STATUS "Installing CSWP client in: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Platform USB include dirs: ${LIBUSB_INCLUDE_DIRS}")
message(STATUS "Platform USB libraries: ${LIBUSB_LINK_LIBRARIES}")

if(ENABLE_DEBUGGING)
  message (STATUS "Building CSWP without optimizations and all debug symbols")
  add_compile_options(-O0 -g3)
endif()

add_subdirectory(client)
add_subdirectory(client_utility)
add_subdirectory(tests)

# KEEP THIS LAST:
# Create a package of the project.
install(
  CODE "
    message(STATUS \"Creating CSWP package: ${CMAKE_INSTALL_PREFIX}/packages/cswp-${CMAKE_SYSTEM_PROCESSOR}-pkg.tar.bz2\")
    execute_process(
      COMMAND mkdir -p ${CMAKE_INSTALL_PREFIX}/packages
      ERROR_QUIET
    )
    execute_process(
      COMMAND tar --exclude-backups -jcf packages/cswp-${CMAKE_SYSTEM_PROCESSOR}-pkg.tar.bz2 ${INSTALLED_FILES}
      WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}
      RESULT_VARIABLE RETVAR
    )
    if (RETVAR)
      message(FATAL_ERROR \"CSWP package creation failed\")
    endif()
  "
)
