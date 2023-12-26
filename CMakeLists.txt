cmake_minimum_required(VERSION 3.27)
project(remote_desktop C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Ofast -Wall")



add_executable(remote_desktop server.c)
add_executable(client client.c)

target_link_libraries(remote_desktop X11 Xtst Xext)
target_link_libraries(client SDL2)
