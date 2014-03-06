
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_definitions(-std=c++0x)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_definitions(-std=c++11)
endif()

include_directories(${greg_SOURCE_DIR}/deps)

list(APPEND greg_SOURCES greg.cpp
                         ${greg_SOURCE_DIR}/deps/pugiconfig.hpp
                         ${greg_SOURCE_DIR}/deps/pugixml.cpp
                         ${greg_SOURCE_DIR}/deps/pugixml.cpp
                         ${greg_SOURCE_DIR}/deps/wire.hpp
                         ${greg_SOURCE_DIR}/deps/wire.cpp)

if (WIN32)
  list(APPEND greg_SOURCES ${greg_SOURCE_DIR}/deps/getopt.h
                           ${greg_SOURCE_DIR}/deps/getopt.c)
endif()

add_executable(greg ${greg_SOURCES})

