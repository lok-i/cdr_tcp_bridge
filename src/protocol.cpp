#include "cdr_tcp_bridge/protocol.hpp"

#include <poll.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>

namespace cdr_tcp_bridge
{
namespace
{

void put_u16(uint8_t * out, uint16_t value)
{
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value);
}

void put_u32(uint8_t * out, uint32_t value)
{
  out[0] = static_cast<uint8_t>(value >> 24);
  out[1] = static_cast<uint8_t>(value >> 16);
  out[2] = static_cast<uint8_t>(value >> 8);
  out[3] = static_cast<uint8_t>(value);
}

void put_u64(uint8_t * out, uint64_t value)
{
  for (int i = 7; i >= 0; --i) {
    out[7 - i] = static_cast<uint8_t>(value >> (i * 8));
  }
}

uint16_t get_u16(const uint8_t * in)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

uint32_t get_u32(const uint8_t * in)
{
  return (static_cast<uint32_t>(in[0]) << 24) |
         (static_cast<uint32_t>(in[1]) << 16) |
         (static_cast<uint32_t>(in[2]) << 8) |
         static_cast<uint32_t>(in[3]);
}

uint64_t get_u64(const uint8_t * in)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | in[i];
  }
  return value;
}

int remaining_ms(std::chrono::steady_clock::time_point deadline)
{
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
    deadline - std::chrono::steady_clock::now()).count();
  if (remaining <= 0) {
    return 0;
  }
  return static_cast<int>(remaining > 2147483647 ? 2147483647 : remaining);
}

IoStatus wait_fd(int fd, short events, std::chrono::steady_clock::time_point deadline)
{
  while (true) {
    pollfd pfd{fd, events, 0};
    const int result = ::poll(&pfd, 1, remaining_ms(deadline));
    if (result == 0) {
      return IoStatus::kTimeout;
    }
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return IoStatus::kError;
    }
    if (pfd.revents & (POLLERR | POLLNVAL)) {
      return IoStatus::kError;
    }
    if (pfd.revents & POLLHUP) {
      return IoStatus::kClosed;
    }
    if (pfd.revents & events) {
      return IoStatus::kOk;
    }
  }
}

IoStatus write_exact(
  int fd, const uint8_t * data, std::size_t size,
  std::chrono::steady_clock::time_point deadline)
{
  std::size_t offset = 0;
  while (offset < size) {
    const auto ready = wait_fd(fd, POLLOUT, deadline);
    if (ready != IoStatus::kOk) {
      return ready;
    }
    const ssize_t sent = ::send(fd, data + offset, size - offset, MSG_NOSIGNAL);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent == 0) {
      return IoStatus::kClosed;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    return (errno == EPIPE || errno == ECONNRESET) ? IoStatus::kClosed : IoStatus::kError;
  }
  return IoStatus::kOk;
}

IoStatus read_exact(
  int fd, uint8_t * data, std::size_t size,
  std::chrono::steady_clock::time_point deadline, bool idle_timeout_ok)
{
  std::size_t offset = 0;
  while (offset < size) {
    const auto ready = wait_fd(fd, POLLIN, deadline);
    if (ready != IoStatus::kOk) {
      if (ready == IoStatus::kTimeout && (offset > 0 || !idle_timeout_ok)) {
        return IoStatus::kError;  // A partial frame cannot be resumed by the caller.
      }
      return ready;
    }
    const ssize_t received = ::recv(fd, data + offset, size - offset, 0);
    if (received > 0) {
      offset += static_cast<std::size_t>(received);
      continue;
    }
    if (received == 0) {
      return IoStatus::kClosed;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    return errno == ECONNRESET ? IoStatus::kClosed : IoStatus::kError;
  }
  return IoStatus::kOk;
}

}  // namespace

std::array<uint8_t, kHeaderSize> encode_header(const FrameHeader & header)
{
  std::array<uint8_t, kHeaderSize> out{};
  put_u32(out.data(), kMagic);
  put_u16(out.data() + 4, kProtocolVersion);
  put_u16(out.data() + 6, static_cast<uint16_t>(kHeaderSize));
  put_u16(out.data() + 8, header.channel_id);
  put_u16(out.data() + 10, header.flags);
  put_u32(out.data() + 12, header.payload_size);
  put_u64(out.data() + 16, header.sequence);
  put_u64(out.data() + 24, header.send_timestamp_ns);
  return out;
}

bool decode_header(
  const uint8_t * data, std::size_t size, std::size_t max_payload,
  FrameHeader & header, std::string & error)
{
  if (size != kHeaderSize) {
    error = "wrong header size";
    return false;
  }
  if (get_u32(data) != kMagic) {
    error = "bad magic";
    return false;
  }
  if (get_u16(data + 4) != kProtocolVersion) {
    error = "unsupported protocol version";
    return false;
  }
  if (get_u16(data + 6) != kHeaderSize) {
    error = "unsupported header size";
    return false;
  }
  header.channel_id = get_u16(data + 8);
  header.flags = get_u16(data + 10);
  header.payload_size = get_u32(data + 12);
  header.sequence = get_u64(data + 16);
  header.send_timestamp_ns = get_u64(data + 24);
  if (header.channel_id == 0) {
    error = "channel id 0 is reserved";
    return false;
  }
  if (header.payload_size > max_payload) {
    error = "payload exceeds configured maximum";
    return false;
  }
  return true;
}

IoStatus send_frame(
  int fd, const FrameHeader & header, const uint8_t * payload,
  std::chrono::milliseconds timeout)
{
  if (header.payload_size > 0 && payload == nullptr) {
    return IoStatus::kInvalid;
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const auto encoded = encode_header(header);
  auto status = write_exact(fd, encoded.data(), encoded.size(), deadline);
  if (status != IoStatus::kOk || header.payload_size == 0) {
    return status;
  }
  return write_exact(fd, payload, header.payload_size, deadline);
}

IoStatus receive_frame(
  int fd, std::size_t max_payload, FrameHeader & header,
  std::vector<uint8_t> & payload, std::chrono::milliseconds timeout,
  std::string & error)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::array<uint8_t, kHeaderSize> encoded{};
  auto status = read_exact(fd, encoded.data(), encoded.size(), deadline, true);
  if (status != IoStatus::kOk) {
    return status;
  }
  if (!decode_header(encoded.data(), encoded.size(), max_payload, header, error)) {
    return IoStatus::kInvalid;
  }
  payload.resize(header.payload_size);
  if (payload.empty()) {
    return IoStatus::kOk;
  }
  return read_exact(fd, payload.data(), payload.size(), deadline, false);
}

uint64_t system_time_ns()
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace cdr_tcp_bridge
