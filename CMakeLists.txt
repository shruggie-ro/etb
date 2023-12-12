cmake_minimum_required(VERSION 2.6)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckFunctionExists)

PROJECT(etb C)

ADD_DEFINITIONS(-Wall -Werror)
IF(CMAKE_C_COMPILER_VERSION VERSION_GREATER 6)
	ADD_DEFINITIONS(-Wextra -Werror=implicit-function-declaration)
	ADD_DEFINITIONS(-Wformat -Werror=format-security -Werror=format-nonliteral)
ENDIF()
ADD_DEFINITIONS(-Os --std=gnu99 -Wmissing-declarations -Wno-unused-parameter -Wno-unused-but-set-parameter)

IF(HAS_IMPLICIT_FALLTHROUGH)
	ADD_DEFINITIONS(-Wimplicit-fallthrough)
ENDIF()

FIND_LIBRARY(websockets NAMES websockets)
FIND_LIBRARY(json NAMES json-c)

SET(LIBS ${websockets} ${json})

SET(SOURCES
	plugins/camera/camera.c
	plugins/camera/protocol.c
	main.c ws_server.c)

ADD_EXECUTABLE(etb ${SOURCES})

INCLUDE_DIRECTORIES(include)

TARGET_LINK_LIBRARIES(etb ${LIBS})

INSTALL(TARGETS etb
	RUNTIME DESTINATION /usr/bin
)
