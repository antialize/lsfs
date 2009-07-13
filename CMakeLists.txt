cmake_minimum_required (VERSION 2.6)
find_package(Boost COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(.)

set(CMAKE_CXX_FLAGS -D_FILE_OFFSET_BITS=64)

add_library(lsfs SHARED lsfs.cc)
add_executable(mkfs.lsfs mkfs.cc)
target_link_libraries(mkfs.lsfs ${Boost_LIBRARIES}  lsfs)

add_executable(lsfs.fuse lsfs-fuse.cc)
target_link_libraries(lsfs.fuse lsfs -lfuse)

add_executable(bench bench.c)

install(TARGETS lsfs mkfs.lsfs lsfs.fuse
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  )

install(FILES lsfs.hh DESTINATION include)

