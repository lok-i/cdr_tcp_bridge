#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdr_tcp_bridge
{

struct ChannelConfig
{
  uint16_t id = 0;
  std::string topic;
  std::string type;
  double max_rate_hz = 0.0;
  int priority = 5;
};

std::vector<ChannelConfig> load_channels(const std::string & path);
void validate_channels(const std::vector<ChannelConfig> & channels);

}  // namespace cdr_tcp_bridge
