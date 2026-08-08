#include "cdr_tcp_bridge/protocol.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cdr_tcp_bridge
{
namespace
{

std::array<int, 2> tcp_socket_pair()
{
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    throw std::runtime_error("socket failed");
  }
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  endpoint.sin_port = 0;
  if (::bind(listener, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint)) < 0 ||
    ::listen(listener, 1) < 0)
  {
    ::close(listener);
    throw std::runtime_error("bind/listen failed");
  }
  socklen_t endpoint_size = sizeof(endpoint);
  if (::getsockname(listener, reinterpret_cast<sockaddr *>(&endpoint), &endpoint_size) < 0) {
    ::close(listener);
    throw std::runtime_error("getsockname failed");
  }
  int client = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client < 0 ||
    ::connect(client, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint)) < 0)
  {
    ::close(listener);
    if (client >= 0) {
      ::close(client);
    }
    throw std::runtime_error("connect failed");
  }
  int server = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (server < 0) {
    ::close(client);
    throw std::runtime_error("accept failed");
  }
  return {client, server};
}

TEST(Protocol, HeaderRoundTripUsesStableNetworkOrder)
{
  FrameHeader input;
  input.channel_id = 0x1234;
  input.flags = 0x5678;
  input.payload_size = 0x01020304;
  input.sequence = 0x0102030405060708ULL;
  input.send_timestamp_ns = 0x1112131415161718ULL;

  const auto bytes = encode_header(input);
  EXPECT_EQ(bytes[0], 0x43);
  EXPECT_EQ(bytes[1], 0x44);
  EXPECT_EQ(bytes[2], 0x52);
  EXPECT_EQ(bytes[3], 0x54);
  EXPECT_EQ(bytes[8], 0x12);
  EXPECT_EQ(bytes[9], 0x34);
  EXPECT_EQ(bytes[12], 0x01);
  EXPECT_EQ(bytes[15], 0x04);

  FrameHeader output;
  std::string error;
  ASSERT_TRUE(decode_header(bytes.data(), bytes.size(), 0x01020304, output, error));
  EXPECT_EQ(output.channel_id, input.channel_id);
  EXPECT_EQ(output.flags, input.flags);
  EXPECT_EQ(output.payload_size, input.payload_size);
  EXPECT_EQ(output.sequence, input.sequence);
  EXPECT_EQ(output.send_timestamp_ns, input.send_timestamp_ns);
}

TEST(Protocol, RejectsBadMagicVersionAndOversizePayload)
{
  FrameHeader input;
  input.channel_id = 1;
  input.payload_size = 100;
  auto bytes = encode_header(input);
  FrameHeader output;
  std::string error;

  bytes[0] = 0;
  EXPECT_FALSE(decode_header(bytes.data(), bytes.size(), 100, output, error));
  EXPECT_EQ(error, "bad magic");

  bytes = encode_header(input);
  bytes[5] = 2;
  EXPECT_FALSE(decode_header(bytes.data(), bytes.size(), 100, output, error));
  EXPECT_EQ(error, "unsupported protocol version");

  bytes = encode_header(input);
  EXPECT_FALSE(decode_header(bytes.data(), bytes.size(), 99, output, error));
  EXPECT_EQ(error, "payload exceeds configured maximum");
}

TEST(Protocol, SendsAndReceivesOneFrameOverSocketPair)
{
  const auto sockets = tcp_socket_pair();

  const std::vector<uint8_t> input = {0, 1, 2, 3, 0xfe, 0xff};
  FrameHeader sent;
  sent.channel_id = 3;
  sent.payload_size = input.size();
  sent.sequence = 42;
  sent.send_timestamp_ns = 99;
  ASSERT_EQ(send_frame(sockets[0], sent, input.data(), std::chrono::milliseconds(100)),
    IoStatus::kOk);

  FrameHeader received;
  std::vector<uint8_t> output;
  std::string error;
  EXPECT_EQ(receive_frame(
      sockets[1], 1024, received, output, std::chrono::milliseconds(100), error),
    IoStatus::kOk);
  EXPECT_EQ(received.channel_id, 3);
  EXPECT_EQ(received.sequence, 42u);
  EXPECT_EQ(received.send_timestamp_ns, 99u);
  EXPECT_EQ(output, input);

  ::close(sockets[0]);
  ::close(sockets[1]);
}

TEST(Protocol, ReceiveTimesOutWithoutData)
{
  const auto sockets = tcp_socket_pair();
  FrameHeader header;
  std::vector<uint8_t> payload;
  std::string error;
  EXPECT_EQ(receive_frame(
      sockets[1], 1024, header, payload, std::chrono::milliseconds(5), error),
    IoStatus::kTimeout);
  ::close(sockets[0]);
  ::close(sockets[1]);
}

TEST(Protocol, PartialHeaderTimeoutInvalidatesTheStream)
{
  const auto sockets = tcp_socket_pair();
  FrameHeader input;
  input.channel_id = 1;
  const auto encoded = encode_header(input);
  ASSERT_EQ(::send(sockets[0], encoded.data(), 5, MSG_NOSIGNAL), 5);

  FrameHeader header;
  std::vector<uint8_t> payload;
  std::string error;
  EXPECT_EQ(receive_frame(
      sockets[1], 1024, header, payload, std::chrono::milliseconds(5), error),
    IoStatus::kError);
  ::close(sockets[0]);
  ::close(sockets[1]);
}

}  // namespace
}  // namespace cdr_tcp_bridge
