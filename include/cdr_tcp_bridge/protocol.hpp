#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdr_tcp_bridge
{

constexpr uint32_t kMagic = 0x43445254U;  // "CDRT"
constexpr uint16_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 32;

struct FrameHeader
{
  uint16_t channel_id = 0;
  uint16_t flags = 0;
  uint32_t payload_size = 0;
  uint64_t sequence = 0;
  uint64_t send_timestamp_ns = 0;
};

enum class IoStatus
{
  kOk,
  kTimeout,
  kClosed,
  kError,
  kInvalid,
};

std::array<uint8_t, kHeaderSize> encode_header(const FrameHeader & header);
bool decode_header(
  const uint8_t * data, std::size_t size, std::size_t max_payload,
  FrameHeader & header, std::string & error);

IoStatus send_frame(
  int fd, const FrameHeader & header, const uint8_t * payload,
  std::chrono::milliseconds timeout);

IoStatus receive_frame(
  int fd, std::size_t max_payload, FrameHeader & header,
  std::vector<uint8_t> & payload, std::chrono::milliseconds timeout,
  std::string & error);

uint64_t system_time_ns();

}  // namespace cdr_tcp_bridge
