#include "matching_engine/object_pool.hpp"

#include <gtest/gtest.h>

#include "matching_engine/order.hpp"

using namespace me;

TEST(ObjectPool, AcquireReturnsDistinctSlotsUpToCapacity) {
  ObjectPool<Order> pool(3);
  Order* a = pool.Acquire();
  Order* b = pool.Acquire();
  Order* c = pool.Acquire();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  EXPECT_NE(a, b);
  EXPECT_NE(b, c);
  EXPECT_EQ(pool.in_use(), 3u);
}

TEST(ObjectPool, ExhaustedPoolReturnsNullptr) {
  ObjectPool<Order> pool(1);
  Order* a = pool.Acquire();
  ASSERT_NE(a, nullptr);
  Order* b = pool.Acquire();
  EXPECT_EQ(b, nullptr);
}

TEST(ObjectPool, ReleaseAllowsReuse) {
  ObjectPool<Order> pool(1);
  Order* a = pool.Acquire();
  a->id = 42;
  pool.Release(a);
  EXPECT_EQ(pool.in_use(), 0u);
  Order* b = pool.Acquire();
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a, b);  // same slot reused
}

TEST(ObjectPool, ConstructorArgsForwarded) {
  struct Point {
    int x, y;
    Point(int a, int b) : x(a), y(b) {}
  };
  ObjectPool<Point> pool(2);
  Point* p = pool.Acquire(3, 4);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->x, 3);
  EXPECT_EQ(p->y, 4);
}

TEST(ObjectPool, ZeroCapacityAlwaysExhausted) {
  ObjectPool<Order> pool(0);
  EXPECT_EQ(pool.Acquire(), nullptr);
  EXPECT_EQ(pool.capacity(), 0u);
}

TEST(ObjectPool, AvailableAndCapacityTrackUsage) {
  ObjectPool<Order> pool(4);
  EXPECT_EQ(pool.capacity(), 4u);
  EXPECT_EQ(pool.available(), 4u);
  Order* a = pool.Acquire();
  Order* b = pool.Acquire();
  EXPECT_EQ(pool.available(), 2u);
  pool.Release(a);
  EXPECT_EQ(pool.available(), 3u);
  pool.Release(b);
  EXPECT_EQ(pool.available(), 4u);
}
