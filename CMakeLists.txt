cmake_minimum_required(VERSION 2.6)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckFunctionExists)

PROJECT(etb C)

set(HTTP_ROOT "${CMAKE_INSTALL_PREFIX}/share/etb/htroot" CACHE STRING "Location of the HTML + JS files")

ADD_DEFINITIONS(-Wall -Werror)
IF(CMAKE_C_COMPILER_VERSION VERSION_GREATER 6)
	ADD_DEFINITIONS(-Wextra -Werror=implicit-function-declaration)
	ADD_DEFINITIONS(-Wformat -Werror=format-security -Werror=format-nonliteral)
ENDIF()
ADD_DEFINITIONS(-Os --std=gnu99 -Wmissing-declarations -Wno-unused-parameter -Wno-unused-but-set-parameter)

IF(HAS_IMPLICIT_FALLTHROUGH)
	ADD_DEFINITIONS(-Wimplicit-fallthrough)
ENDIF()

ADD_DEFINITIONS(-DHTTP_ROOT="${HTTP_ROOT}")

FIND_LIBRARY(websockets NAMES websockets)
FIND_LIBRARY(json NAMES json-c)
FIND_LIBRARY(turbojpeg NAMES turbojpeg)

SET(LIBS ${websockets} ${json} ${turbojpeg})

SET(SOURCES
	plugins/camera/camera.c
	plugins/camera/jpeg.c
	plugins/camera/protocol.c
	main.c ws_server.c)

ADD_EXECUTABLE(etb ${SOURCES})

TARGET_LINK_LIBRARIES(etb ${LIBS})

INSTALL(TARGETS etb
	RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)
INSTALL(DIRECTORY client/
	DESTINATION ${HTTP_ROOT}
)
