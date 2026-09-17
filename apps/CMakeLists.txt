add_executable(gateway_server gateway_server.cpp)
target_link_libraries(gateway_server PRIVATE me_net me_persistence Threads::Threads)

add_executable(send_order send_order.cpp)
target_link_libraries(send_order PRIVATE me_net)
