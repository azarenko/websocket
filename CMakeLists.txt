cmake_minimum_required(VERSION 2.6)
project(websocket)

add_executable(websocket main.c cmdparam.c settings.c sockutils.c proto.c cJSON.c fifo.c)
target_link_libraries(websocket websockets pthread config pq m)
include_directories(/usr/include/postgresql)

install(TARGETS websocket RUNTIME DESTINATION bin)