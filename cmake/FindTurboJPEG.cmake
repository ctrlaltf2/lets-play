# - Find TURBOJPEG
# Find the libjpeg-turbo includes and library
# This module defines
#  TURBOJPEG_INCLUDE_DIR, where to find jpeglib.h and turbojpeg.h, etc.
#  TURBOJPEG_LIBRARIES, the libraries needed to use libjpeg-turbo.
#  TURBOJPEG_FOUND, If false, do not try to use libjpeg-turbo.
# also defined, but not for general use are
#  TURBOJPEG_LIBRARY, where to find the libjpeg-turbo library.

#=============================================================================
# Copyright 2001-2009 Kitware, Inc.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)

find_path(TURBOJPEG_PREFIX "include/turbojpeg.h"
  $ENV{TURBOJPEG_HOME}
  $ENV{EXTERNLIBS}/libjpeg-turbo64
  $ENV{EXTERNLIBS}/libjpeg-turbo
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/opt/jpeg-turbo # Homebrew
  /usr/local
  /usr
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
  DOC "TURBOJPEG - Prefix"
)


FIND_PATH(TURBOJPEG_INCLUDE_DIR "turbojpeg.h"
  HINTS ${TURBOJPEG_PREFIX}/include
  PATHS
  $ENV{TURBOJPEG_HOME}/include
  $ENV{EXTERNLIBS}/libjpeg-turbo64/include
  $ENV{EXTERNLIBS}/libjpeg-turbo/include
  ~/Library/Frameworks/include
  /Library/Frameworks/include
  /usr/local/opt/jpeg-turbo/include
  /usr/local/include
  /usr/include
  /sw/include # Fink
  /opt/local/include # DarwinPorts
  /opt/csw/include # Blastwave
  /opt/include
  DOC "TURBOJPEG - Headers"
)

FIND_PATH(TURBOJPEG_INCLUDE_DIR_INT "jpegint.h"
   PATHS ${TURBOJPEG_INCLUDE_DIR}
   DOC "TURBOJPEG - Internal Headers"
)

FIND_LIBRARY(TURBOJPEG_LIBRARY turbojpeg
  HINTS ${TURBOJPEG_PREFIX}/lib ${TURBOJPEG_PREFIX}/lib64
  PATHS
  $ENV{TURBOJPEG_HOME}
  $ENV{EXTERNLIBS}/libjpeg-turbo64
  $ENV{EXTERNLIBS}/libjpeg-turbo
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr/local/opt/jpeg-turbo
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
  PATH_SUFFIXES lib lib64
  DOC "TURBOJPEG - Library"
)


if (MSVC)
    FIND_LIBRARY(TURBOJPEG_LIBRARY_DEBUG turbojpegd
        HINTS ${TURBOJPEG_PREFIX}/debug/lib ${TURBOJPEG_PREFIX}/debug/lib64 ${TURBOJPEG_PREFIX}/lib ${TURBOJPEG_PREFIX}/lib64
        PATHS
        $ENV{TURBOJPEG_HOME}
        $ENV{EXTERNLIBS}/libjpeg-turbo64
        $ENV{EXTERNLIBS}/libjpeg-turbo
        ~/Library/Frameworks
        /Library/Frameworks
        /usr/local
        /usr/local/opt/jpeg-turbo
        /usr
        /sw
        /opt/local
        /opt/csw
        /opt
        PATH_SUFFIXES debug/lib debug/lib64 lib lib64
        DOC "TURBOJPEG - Library"
    )
endif()


# handle the QUIETLY and REQUIRED arguments and set TURBOJPEG_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
if (MSVC)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(TURBOJPEG DEFAULT_MSG
        TURBOJPEG_INCLUDE_DIR
        TURBOJPEG_LIBRARY TURBOJPEG_LIBRARY_DEBUG
        TURBOJPEG_LIBRARY TURBOJPEG_LIBRARY_DEBUG)
else()
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(TURBOJPEG DEFAULT_MSG TURBOJPEG_LIBRARY TURBOJPEG_LIBRARY TURBOJPEG_INCLUDE_DIR)
endif()

IF(TURBOJPEG_FOUND)
    if (MSVC)
        SET(TURBOJPEG_LIBRARIES optimized ${TURBOJPEG_LIBRARY} debug ${TURBOJPEG_LIBRARY_DEBUG})
        SET(TURBOJPEG_LIBRARIES optimized ${TURBOJPEG_LIBRARY} debug ${TURBOJPEG_LIBRARY_DEBUG})
    else()
        SET(TURBOJPEG_LIBRARIES ${TURBOJPEG_LIBRARY})
        SET(TURBOJPEG_LIBRARIES ${TURBOJPEG_LIBRARY})
  endif()

  INCLUDE (CheckSymbolExists)
  set(CMAKE_REQUIRED_INCLUDES ${TURBOJPEG_INCLUDE_DIR})
  CHECK_SYMBOL_EXISTS(tjMCUWidth "turbojpeg.h" TURBOJPEG_HAVE_TJMCUWIDTH)

  if (TURBOJPEG_INCLUDE_DIR_INT)
     set(TURBOJPEG_HAVE_INTERNAL TRUE)
  else()
     set(TURBOJPEG_HAVE_INTERNAL FALSE)
  endif()

ENDIF(TURBOJPEG_FOUND)

MARK_AS_ADVANCED(TURBOJPEG_LIBRARY TURBOJPEG_LIBRARY TURBOJPEG_INCLUDE_DIR TURBOJPEG_HAVE_TJMCUWIDTH TURBOJPEG_HAVE_INTERNAL)
