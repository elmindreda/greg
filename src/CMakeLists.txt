
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_definitions(-std=c++0x)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_definitions(-std=c++11)
endif()

include_directories(${greg_SOURCE_DIR}/deps)

add_executable(greg greg.cpp
                    ${greg_SOURCE_DIR}/deps/getopt.c
                    ${greg_SOURCE_DIR}/deps/pugixml.cpp
                    ${greg_SOURCE_DIR}/deps/wire.cpp)

