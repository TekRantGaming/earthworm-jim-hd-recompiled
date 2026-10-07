cmake_minimum_required(VERSION 3.25)
project(ewj_dump_image LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(rexglue 0.10.0 REQUIRED CONFIG)
add_executable(ewj_dump_image main.cpp)
target_link_libraries(ewj_dump_image PRIVATE rex::runtime)
add_custom_command(TARGET ewj_dump_image POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ewj_dump_image> $<TARGET_FILE_DIR:ewj_dump_image>
  COMMAND_EXPAND_LISTS)
