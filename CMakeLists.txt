cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(buildingextractor)

set(CMAKE_CXX_STANDARD 11)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(Osmium REQUIRED COMPONENTS io pbf xml)

if(NOT OSMIUM_FOUND)
    message(WARNING "Libosmium not found!\n")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(EXPAT REQUIRED)
find_package(BZip2 REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(${EXPAT_INCLUDE_DIRS})
include_directories(${BZIP_INCLUDE_DIRS})
  include_directories(${ZLIB_INCLUDE_DIRS})

add_executable(buildingextractor src/main.cpp)

target_link_libraries(buildingextractor Threads::Threads)
target_link_libraries(buildingextractor ${EXPAT_LIBRARIES})
target_link_libraries(buildingextractor ${BZIP2_LIBRARIES})
target_link_libraries(buildingextractor ${ZLIB_LIBRARIES})
