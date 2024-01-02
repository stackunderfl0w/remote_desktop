cmake_minimum_required(VERSION 3.27)
project(remote_desktop C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Ofast -mtune=native -Wall")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Ofast -mtune=native -Wall -ftree-vectorize -fdump-tree-vect-details")



add_executable(remote_desktop server.c)
add_executable(client client.c)

target_link_libraries(remote_desktop X11 Xtst Xext Xrandr)
target_link_libraries(client SDL2 X11)
