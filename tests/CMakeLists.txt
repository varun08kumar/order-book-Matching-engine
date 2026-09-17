add_executable(me_tests
  test_naive_order_book.cpp
  test_object_pool.cpp
  test_matching_engine.cpp
  test_spsc_queue.cpp
  test_protocol.cpp
  test_frame_decoder.cpp
  test_tcp_gateway.cpp
  test_journal_and_snapshot.cpp
  test_market_data_publisher.cpp
  test_async_journal_writer.cpp
  test_fast_order_book.cpp
  test_naive_vs_fast_equivalence.cpp
)
target_link_libraries(me_tests PRIVATE me_persistence me_fast me_naive gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(me_tests)
