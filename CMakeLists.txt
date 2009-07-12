cmake_minimum_required (VERSION 2.6)
find_package(Boost COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(.)

SET(CMAKE_CXX_FLAGS -D_FILE_OFFSET_BITS=64)

ADD_LIBRARY(lsfs SHARED lsfs.cc)
ADD_EXECUTABLE(mkfs.lsfs mkfs.cc)
target_link_libraries(mkfs.lsfs ${Boost_LIBRARIES}  lsfs)

ADD_EXECUTABLE(lsfs.fuse lsfs-fuse.cc)
target_link_libraries(lsfs.fuse lsfs -lfuse)

