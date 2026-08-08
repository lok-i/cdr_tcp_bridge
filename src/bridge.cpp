#include "cdr_tcp_bridge/config.hpp"
#include "cdr_tcp_bridge/generic_compat.hpp"
#include "cdr_tcp_bridge/latest_slot.hpp"
#include "cdr_tcp_bridge/protocol.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cdr_tcp_bridge
{
namespace
{

using namespace std::chrono_literals;

void close_socket(int & fd)
{
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

void make_nonblocking(int fd)
{
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    throw std::runtime_error("failed to make socket nonblocking: " +
            std::string(std::strerror(errno)));
  }
}

void configure_stream_socket(int fd, int buffer_bytes)
{
  const int enabled = 1;
  (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
  (void)::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
  if (buffer_bytes > 0) {
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_bytes, sizeof(buffer_bytes));
  }
  make_nonblocking(fd);
}

sockaddr_in ipv4_endpoint(const std::string & address, uint16_t port)
{
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port);
  if (::inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1) {
    throw std::runtime_error("invalid IPv4 address '" + address + "'");
  }
  return endpoint;
}

int create_listener(const std::string & address, uint16_t port)
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
  }
  try {
    const int enabled = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    const auto endpoint = ipv4_endpoint(address, port);
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint)) < 0) {
      throw std::runtime_error("bind " + address + ":" + std::to_string(port) + ": " +
              std::string(std::strerror(errno)));
    }
    if (::listen(fd, 1) < 0) {
      throw std::runtime_error("listen: " + std::string(std::strerror(errno)));
    }
    make_nonblocking(fd);
  } catch (...) {
    close_socket(fd);
    throw;
  }
  return fd;
}

int accept_one(int listener_fd, int timeout_ms, int send_buffer_bytes, std::string & peer)
{
  pollfd pfd{listener_fd, POLLIN, 0};
  const int ready = ::poll(&pfd, 1, timeout_ms);
  if (ready <= 0 || !(pfd.revents & POLLIN)) {
    return -1;
  }

  sockaddr_in address{};
  socklen_t address_size = sizeof(address);
  int fd = ::accept(listener_fd, reinterpret_cast<sockaddr *>(&address), &address_size);
  if (fd < 0) {
    return -1;
  }
  try {
    configure_stream_socket(fd, send_buffer_bytes);
  } catch (...) {
    close_socket(fd);
    throw;
  }
  char ip[INET_ADDRSTRLEN]{};
  (void)::inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
  peer = std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
  return fd;
}

int connect_one(
  const std::string & address, uint16_t port, int timeout_ms, int send_buffer_bytes)
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  try {
    make_nonblocking(fd);
    const auto endpoint = ipv4_endpoint(address, port);
    const int result = ::connect(
      fd, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint));
    if (result < 0 && errno != EINPROGRESS) {
      close_socket(fd);
      return -1;
    }
    if (result < 0) {
      pollfd pfd{fd, POLLOUT, 0};
      if (::poll(&pfd, 1, timeout_ms) <= 0) {
        close_socket(fd);
        return -1;
      }
      int socket_error = 0;
      socklen_t size = sizeof(socket_error);
      if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) < 0 ||
        socket_error != 0)
      {
        close_socket(fd);
        return -1;
      }
    }
    configure_stream_socket(fd, send_buffer_bytes);
  } catch (...) {
    close_socket(fd);
    throw;
  }
  return fd;
}

struct ChannelRuntime
{
  explicit ChannelRuntime(ChannelConfig channel)
  : config(std::move(channel)), slot(config.max_rate_hz)
  {
  }

  ChannelConfig config;
  LatestSlot<rclcpp::SerializedMessage> slot;
#if !CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC
  // Keep the dynamically loaded type support alive until after both endpoints
  // have been destroyed (members are destroyed in reverse declaration order).
  std::shared_ptr<rcpputils::SharedLibrary> typesupport_library;
#endif
  GenericSubscription::SharedPtr subscription;
  GenericPublisher::SharedPtr publisher;
  std::atomic<uint64_t> sent{0};
  std::atomic<uint64_t> published{0};
  std::atomic<uint64_t> oversize{0};
};

}  // namespace

class BridgeNode final : public rclcpp::Node
{
public:
  BridgeNode()
  : Node("cdr_tcp_bridge")
  {
    const auto share = ament_index_cpp::get_package_share_directory("cdr_tcp_bridge");
    role_ = this->declare_parameter<std::string>("role", "server");
    config_path_ = this->declare_parameter<std::string>(
      "config_path", share + "/config/light.yaml");
    bind_address_ = this->declare_parameter<std::string>("bind_address", "127.0.0.1");
    server_address_ = this->declare_parameter<std::string>("server_address", "127.0.0.1");
    const int port_value = this->declare_parameter<int>("port", 5556);
    send_timeout_ms_ = this->declare_parameter<int>("send_timeout_ms", 20);
    receive_timeout_ms_ = this->declare_parameter<int>("receive_timeout_ms", 1000);
    reconnect_ms_ = this->declare_parameter<int>("reconnect_ms", 1000);
    socket_buffer_bytes_ = this->declare_parameter<int>("socket_buffer_bytes", 262144);
    const int64_t max_payload = this->declare_parameter<int64_t>(
      "max_payload_bytes", 8 * 1024 * 1024);

    if (role_ != "server" && role_ != "client") {
      throw std::runtime_error("role must be 'server' or 'client'");
    }
    if (port_value < 1 || port_value > 65535) {
      throw std::runtime_error("port must be in [1, 65535]");
    }
    if (send_timeout_ms_ < 1 || receive_timeout_ms_ < 1 || reconnect_ms_ < 1) {
      throw std::runtime_error("timeouts and reconnect_ms must be positive");
    }
    if (max_payload < static_cast<int64_t>(kHeaderSize) ||
      max_payload > static_cast<int64_t>(UINT32_MAX))
    {
      throw std::runtime_error("max_payload_bytes is outside the supported range");
    }
    port_ = static_cast<uint16_t>(port_value);
    max_payload_ = static_cast<std::size_t>(max_payload);

    for (auto & config : load_channels(config_path_)) {
      channels_.push_back(std::make_unique<ChannelRuntime>(std::move(config)));
    }
    std::sort(channels_.begin(), channels_.end(), [](const auto & lhs, const auto & rhs) {
      return lhs->config.priority < rhs->config.priority;
    });
    for (auto & channel : channels_) {
      by_id_.emplace(channel->config.id, channel.get());
    }

#if CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC
    RCLCPP_INFO(this->get_logger(), "generic endpoint backend: native rclcpp");
#else
    RCLCPP_INFO(this->get_logger(), "generic endpoint backend: Foxy compatibility");
#endif

    const auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
    if (role_ == "server") {
      configure_server(qos);
    } else {
      configure_client(qos);
    }

    stats_timer_ = this->create_wall_timer(5s, std::bind(&BridgeNode::log_stats, this));
    running_.store(true);
    network_thread_ = std::thread(&BridgeNode::network_loop, this);
  }

  ~BridgeNode() override
  {
    running_.store(false);
    wake_cv_.notify_all();
    if (network_thread_.joinable()) {
      network_thread_.join();
    }
  }

private:
  void configure_server(const rclcpp::QoS & qos)
  {
    listener_fd_ = create_listener(bind_address_, port_);
    for (auto & channel : channels_) {
      auto * runtime = channel.get();
#if CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC
      runtime->subscription = this->create_generic_subscription(
        runtime->config.topic, runtime->config.type, qos,
        [this, runtime](std::shared_ptr<rclcpp::SerializedMessage> message) {
          if (message->size() > max_payload_) {
            ++runtime->oversize;
            return;
          }
          runtime->slot.offer(std::move(message));
          wake_cv_.notify_one();
        });
#else
      const auto type_support = load_typesupport(runtime->config.type);
      runtime->typesupport_library = type_support.library;
      runtime->subscription = std::make_shared<GenericSubscription>(
        this->get_node_base_interface().get(), *type_support.handle,
        runtime->config.topic, qos,
        [this, runtime](std::shared_ptr<rclcpp::SerializedMessage> message) {
          if (message->size() > max_payload_) {
            ++runtime->oversize;
            return;
          }
          runtime->slot.offer(std::move(message));
          wake_cv_.notify_one();
        });
      this->get_node_topics_interface()->add_subscription(runtime->subscription, nullptr);
#endif
      RCLCPP_INFO(
        this->get_logger(), "TX channel %u: %s [%s] <= %.1f Hz priority %d",
        runtime->config.id, runtime->config.topic.c_str(), runtime->config.type.c_str(),
        runtime->config.max_rate_hz, runtime->config.priority);
    }
    RCLCPP_INFO(
      this->get_logger(), "CDR/TCP server listening on %s:%u (max payload %zu bytes)",
      bind_address_.c_str(), port_, max_payload_);
  }

  void configure_client(const rclcpp::QoS & qos)
  {
    for (auto & channel : channels_) {
#if CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC
      channel->publisher = this->create_generic_publisher(
        channel->config.topic, channel->config.type, qos);
#else
      const auto type_support = load_typesupport(channel->config.type);
      channel->typesupport_library = type_support.library;
      channel->publisher = std::make_shared<GenericPublisher>(
        this->get_node_base_interface().get(), *type_support.handle,
        channel->config.topic, qos);
#endif
      RCLCPP_INFO(
        this->get_logger(), "RX channel %u: %s [%s]",
        channel->config.id, channel->config.topic.c_str(), channel->config.type.c_str());
    }
    RCLCPP_INFO(
      this->get_logger(), "CDR/TCP client will connect to %s:%u (max payload %zu bytes)",
      server_address_.c_str(), port_, max_payload_);
  }

  void network_loop()
  {
    if (role_ == "server") {
      server_loop();
    } else {
      client_loop();
    }
  }

  void server_loop()
  {
    while (running_.load()) {
      if (peer_fd_ < 0) {
        std::string peer;
        peer_fd_ = accept_one(listener_fd_, 250, socket_buffer_bytes_, peer);
        if (peer_fd_ >= 0) {
          RCLCPP_INFO(this->get_logger(), "telemetry client connected: %s", peer.c_str());
        }
        continue;
      }

      bool sent_any = false;
      const auto now = std::chrono::steady_clock::now();
      for (auto & channel : channels_) {
        auto item = channel->slot.take_if_due(now);
        if (!item.value) {
          continue;
        }
        const auto & raw = item.value->get_rcl_serialized_message();
        FrameHeader header;
        header.channel_id = channel->config.id;
        header.payload_size = static_cast<uint32_t>(raw.buffer_length);
        header.sequence = item.sequence;
        header.send_timestamp_ns = system_time_ns();
        const auto status = send_frame(
          peer_fd_, header, raw.buffer, std::chrono::milliseconds(send_timeout_ms_));
        if (status != IoStatus::kOk) {
          RCLCPP_WARN(
            this->get_logger(), "telemetry client stalled/disconnected; dropping connection");
          close_socket(peer_fd_);
          break;
        }
        ++channel->sent;
        sent_any = true;
      }

      if (!sent_any && peer_fd_ >= 0) {
        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_cv_.wait_for(lock, 2ms, [this]() {return !running_.load();});
      }
    }
    close_socket(peer_fd_);
    close_socket(listener_fd_);
  }

  void client_loop()
  {
    while (running_.load()) {
      if (peer_fd_ < 0) {
        peer_fd_ = connect_one(
          server_address_, port_, reconnect_ms_, socket_buffer_bytes_);
        if (peer_fd_ < 0) {
          std::unique_lock<std::mutex> lock(wake_mutex_);
          wake_cv_.wait_for(
            lock, std::chrono::milliseconds(reconnect_ms_),
            [this]() {return !running_.load();});
          continue;
        }
        RCLCPP_INFO(
          this->get_logger(), "connected to telemetry server %s:%u",
          server_address_.c_str(), port_);
      }

      FrameHeader header;
      std::vector<uint8_t> payload;
      std::string error;
      const auto status = receive_frame(
        peer_fd_, max_payload_, header, payload,
        std::chrono::milliseconds(receive_timeout_ms_), error);
      if (status == IoStatus::kTimeout) {
        continue;  // An idle telemetry stream is healthy; keep the TCP connection open.
      }
      if (status == IoStatus::kInvalid) {
        RCLCPP_ERROR(this->get_logger(), "invalid telemetry frame: %s", error.c_str());
        close_socket(peer_fd_);
        continue;
      }
      if (status != IoStatus::kOk) {
        RCLCPP_WARN(this->get_logger(), "telemetry server disconnected; reconnecting");
        close_socket(peer_fd_);
        continue;
      }

      const auto found = by_id_.find(header.channel_id);
      if (found == by_id_.end()) {
        RCLCPP_WARN(
          this->get_logger(), "ignoring unconfigured channel %u", header.channel_id);
        continue;
      }
      auto * channel = found->second;
      rclcpp::SerializedMessage message(payload.size());
      auto & raw = message.get_rcl_serialized_message();
      if (!payload.empty()) {
        std::memcpy(raw.buffer, payload.data(), payload.size());
      }
      raw.buffer_length = payload.size();
      channel->publisher->publish(message);
      ++channel->published;
    }
    close_socket(peer_fd_);
  }

  void log_stats()
  {
    for (const auto & channel : channels_) {
      if (role_ == "server") {
        RCLCPP_INFO(
          this->get_logger(),
          "TX %s: received=%lu sent=%lu overwritten=%lu oversize=%lu",
          channel->config.topic.c_str(), channel->slot.received(), channel->sent.load(),
          channel->slot.dropped(), channel->oversize.load());
      } else {
        RCLCPP_INFO(
          this->get_logger(), "RX %s: published=%lu",
          channel->config.topic.c_str(), channel->published.load());
      }
    }
  }

  std::string role_;
  std::string config_path_;
  std::string bind_address_;
  std::string server_address_;
  uint16_t port_ = 5556;
  int send_timeout_ms_ = 20;
  int receive_timeout_ms_ = 1000;
  int reconnect_ms_ = 1000;
  int socket_buffer_bytes_ = 262144;
  std::size_t max_payload_ = 8 * 1024 * 1024;

  std::vector<std::unique_ptr<ChannelRuntime>> channels_;
  std::unordered_map<uint16_t, ChannelRuntime *> by_id_;
  rclcpp::TimerBase::SharedPtr stats_timer_;

  std::atomic<bool> running_{false};
  std::thread network_thread_;
  int listener_fd_ = -1;
  int peer_fd_ = -1;
  std::mutex wake_mutex_;
  std::condition_variable wake_cv_;
};

}  // namespace cdr_tcp_bridge

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<cdr_tcp_bridge::BridgeNode>());
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("cdr_tcp_bridge"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
