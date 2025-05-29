cmake_minimum_required (VERSION 3.10)

#build qpextract
add_executable(qpextract qpextract.cc)

target_include_directories(qpextract
  PRIVATE ${CMAKE_SOURCE_DIR}/libde265
)

target_link_libraries(qpextract PRIVATE de265)

