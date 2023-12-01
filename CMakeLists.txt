cmake_minimum_required(VERSION 2.6)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckFunctionExists)

PROJECT(eak C)

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

SET(LIBS ${websockets})

SET(SOURCES
	main.c ws_server.c)

ADD_EXECUTABLE(eak ${SOURCES})

TARGET_LINK_LIBRARIES(eak ${LIBS})

INSTALL(TARGETS eak
	RUNTIME DESTINATION /usr/bin
)
