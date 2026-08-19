#include "cdr_tcp_bridge/config.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace cdr_tcp_bridge
{
namespace
{

TEST(Config, LoadsChannelDefaultsAndOverrides)
{
  const auto channels = load_channels(TEST_CONFIG_PATH);
  ASSERT_EQ(channels.size(), 2u);
  EXPECT_EQ(channels[0].id, 7);
  EXPECT_EQ(channels[0].topic, "/test/image");
  EXPECT_EQ(channels[0].type, "sensor_msgs/msg/Image");
  EXPECT_DOUBLE_EQ(channels[0].max_rate_hz, 12.5);
  EXPECT_EQ(channels[0].priority, 2);
  EXPECT_DOUBLE_EQ(channels[1].max_rate_hz, 0.0);
  EXPECT_EQ(channels[1].priority, 5);
}

TEST(Config, LoadsReposeRgbdCalibrationProfile)
{
  const auto channels = load_channels(TEST_REPOSE_CALIB_CONFIG_PATH);
  ASSERT_EQ(channels.size(), 8u);
  EXPECT_EQ(channels[0].topic, "/vibe/planner/observe/color/compressed");
  EXPECT_EQ(channels[1].topic, "/vibe/planner/observe/depth");
  EXPECT_EQ(channels[1].type, "sensor_msgs/msg/Image");
  EXPECT_DOUBLE_EQ(channels[1].max_rate_hz, 0.0);
  EXPECT_EQ(channels[4].type, "cpp_control/msg/CubeObservation");
  EXPECT_EQ(channels[6].topic, "/vibe/controller/status");
  EXPECT_EQ(channels[7].topic, "/vibe/planner/calibration/click");
  EXPECT_EQ(channels[7].type, "std_msgs/msg/UInt64");
}

TEST(Config, RejectsDuplicateIdsAndInvalidTopics)
{
  const ChannelConfig valid{1, "/valid", "std_msgs/msg/String", 0.0, 5};
  auto duplicate = valid;
  duplicate.topic = "/other";
  EXPECT_THROW(validate_channels({valid, duplicate}), std::runtime_error);

  auto relative = valid;
  relative.topic = "relative";
  EXPECT_THROW(validate_channels({relative}), std::runtime_error);
}

TEST(Config, RejectsInvalidRatesAndPriorities)
{
  ChannelConfig channel{1, "/valid", "std_msgs/msg/String", -1.0, 5};
  EXPECT_THROW(validate_channels({channel}), std::runtime_error);
  channel.max_rate_hz = 1.0;
  channel.priority = 0;
  EXPECT_THROW(validate_channels({channel}), std::runtime_error);
}

}  // namespace
}  // namespace cdr_tcp_bridge
