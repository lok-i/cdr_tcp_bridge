#include "cdr_tcp_bridge/latest_slot.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

namespace cdr_tcp_bridge
{
namespace
{

TEST(LatestSlot, OverwritesPendingValueWithoutGrowing)
{
  LatestSlot<int> slot;
  slot.offer(std::make_shared<int>(10));
  slot.offer(std::make_shared<int>(20));

  const auto item = slot.take_if_due(std::chrono::steady_clock::now());
  ASSERT_TRUE(item.value);
  EXPECT_EQ(*item.value, 20);
  EXPECT_EQ(item.sequence, 2u);
  EXPECT_EQ(slot.received(), 2u);
  EXPECT_EQ(slot.dropped(), 1u);
  EXPECT_EQ(slot.taken(), 1u);
  EXPECT_FALSE(slot.take_if_due(std::chrono::steady_clock::now()).value);
}

TEST(LatestSlot, KeepsNewestValueUntilRateLimitIsDue)
{
  LatestSlot<int> slot(10.0);
  const auto start = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);

  slot.offer(std::make_shared<int>(1));
  ASSERT_TRUE(slot.take_if_due(start).value);

  slot.offer(std::make_shared<int>(2));
  EXPECT_FALSE(slot.take_if_due(start + std::chrono::milliseconds(99)).value);
  const auto item = slot.take_if_due(start + std::chrono::milliseconds(100));
  ASSERT_TRUE(item.value);
  EXPECT_EQ(*item.value, 2);
}

}  // namespace
}  // namespace cdr_tcp_bridge
