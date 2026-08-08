#include "cdr_tcp_bridge/config.hpp"

#include <yaml-cpp/yaml.h>

#include <set>
#include <stdexcept>
#include <string>

namespace cdr_tcp_bridge
{

void validate_channels(const std::vector<ChannelConfig> & channels)
{
  if (channels.empty()) {
    throw std::runtime_error("channel configuration is empty");
  }

  std::set<uint16_t> ids;
  std::set<std::string> topics;
  for (const auto & channel : channels) {
    if (channel.id == 0) {
      throw std::runtime_error("channel id 0 is reserved");
    }
    if (!ids.insert(channel.id).second) {
      throw std::runtime_error("duplicate channel id " + std::to_string(channel.id));
    }
    if (channel.topic.empty() || channel.topic.front() != '/') {
      throw std::runtime_error("channel topic must be absolute: '" + channel.topic + "'");
    }
    if (!topics.insert(channel.topic).second) {
      throw std::runtime_error("duplicate channel topic '" + channel.topic + "'");
    }
    if (channel.type.empty() || channel.type.find("/msg/") == std::string::npos) {
      throw std::runtime_error("invalid ROS message type '" + channel.type + "'");
    }
    if (channel.max_rate_hz < 0.0) {
      throw std::runtime_error("max_rate_hz cannot be negative for '" + channel.topic + "'");
    }
    if (channel.priority < 1 || channel.priority > 7) {
      throw std::runtime_error("priority must be in [1, 7] for '" + channel.topic + "'");
    }
  }
}

std::vector<ChannelConfig> load_channels(const std::string & path)
{
  const YAML::Node root = YAML::LoadFile(path);
  const YAML::Node entries = root["channels"];
  if (!entries || !entries.IsSequence()) {
    throw std::runtime_error("config must contain a 'channels' sequence");
  }

  std::vector<ChannelConfig> channels;
  channels.reserve(entries.size());
  for (const auto & entry : entries) {
    ChannelConfig channel;
    channel.id = entry["id"].as<uint16_t>();
    channel.topic = entry["topic"].as<std::string>();
    channel.type = entry["type"].as<std::string>();
    channel.max_rate_hz = entry["max_rate_hz"] ? entry["max_rate_hz"].as<double>() : 0.0;
    channel.priority = entry["priority"] ? entry["priority"].as<int>() : 5;
    channels.push_back(std::move(channel));
  }
  validate_channels(channels);
  return channels;
}

}  // namespace cdr_tcp_bridge
