add_library(me_naive
  naive/order_book.cpp
)
target_link_libraries(me_naive PUBLIC matching_engine_core)
target_include_directories(me_naive PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(me_fast
  fast/order_book.cpp
)
target_link_libraries(me_fast PUBLIC matching_engine_core)
target_include_directories(me_fast PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(me_engine
  matching_engine.cpp
)
target_link_libraries(me_engine PUBLIC me_naive)

add_library(me_net
  net/tcp_socket.cpp
  net/protocol.cpp
  net/gateway_session.cpp
)
target_link_libraries(me_net PUBLIC me_engine Threads::Threads)

add_library(me_persistence
  persistence/journal.cpp
  persistence/snapshot.cpp
)
target_link_libraries(me_persistence PUBLIC me_net)
