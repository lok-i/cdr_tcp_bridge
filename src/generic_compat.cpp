// Copyright 2018 Bosch Software Innovations GmbH.
// Copyright 2026 cdr_tcp_bridge contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cdr_tcp_bridge/generic_compat.hpp"

#if !CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC

#include <ament_index_cpp/get_package_prefix.hpp>
#include <rcl/error_handling.h>
#include <rcl/publisher.h>
#include <rcl/subscription.h>
#include <rclcpp/exceptions.hpp>

#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace cdr_tcp_bridge
{
namespace
{

rcl_subscription_options_t subscription_options(const rclcpp::QoS & qos)
{
  auto options = rcl_subscription_get_default_options();
  options.qos = qos.get_rmw_qos_profile();
  return options;
}

rcl_publisher_options_t publisher_options(const rclcpp::QoS & qos)
{
  auto options = rcl_publisher_get_default_options();
  options.qos = qos.get_rmw_qos_profile();
  return options;
}

std::tuple<std::string, std::string, std::string> split_type(const std::string & type)
{
  const auto first = type.find('/');
  const auto last = type.rfind('/');
  if (first == std::string::npos || last == 0 || last == type.size() - 1) {
    throw std::runtime_error(
            "message type '" + type + "' must be package/msg/Type or package/Type");
  }

  const auto package = type.substr(0, first);
  const auto module = first == last ? std::string("msg") : type.substr(first + 1, last - first - 1);
  const auto name = type.substr(last + 1);
  if (package.empty() || module.empty() || name.empty()) {
    throw std::runtime_error("invalid message type '" + type + "'");
  }
  return std::make_tuple(package, module, name);
}

}  // namespace

GenericSubscription::GenericSubscription(
  rclcpp::node_interfaces::NodeBaseInterface * node_base,
  const rosidl_message_type_support_t & type_support,
  const std::string & topic_name,
  const rclcpp::QoS & qos,
  std::function<void(std::shared_ptr<rclcpp::SerializedMessage>)> callback)
: SubscriptionBase(
    node_base, type_support, topic_name, subscription_options(qos), true),
  callback_(std::move(callback))
{
}

std::shared_ptr<void> GenericSubscription::create_message()
{
  return create_serialized_message();
}

std::shared_ptr<rclcpp::SerializedMessage> GenericSubscription::create_serialized_message()
{
  return std::make_shared<rclcpp::SerializedMessage>(0);
}

void GenericSubscription::handle_message(
  std::shared_ptr<void> & message, const rclcpp::MessageInfo & message_info)
{
  (void)message_info;
  callback_(std::static_pointer_cast<rclcpp::SerializedMessage>(message));
}

void GenericSubscription::handle_serialized_message(
  const std::shared_ptr<rclcpp::SerializedMessage> & message,
  const rclcpp::MessageInfo & message_info)
{
  (void)message_info;
  callback_(message);
}

void GenericSubscription::handle_loaned_message(
  void * loaned_message, const rclcpp::MessageInfo & message_info)
{
  (void)loaned_message;
  (void)message_info;
}

void GenericSubscription::return_message(std::shared_ptr<void> & message)
{
  auto serialized = std::static_pointer_cast<rclcpp::SerializedMessage>(message);
  return_serialized_message(serialized);
}

void GenericSubscription::return_serialized_message(
  std::shared_ptr<rclcpp::SerializedMessage> & message)
{
  message.reset();
}

GenericPublisher::GenericPublisher(
  rclcpp::node_interfaces::NodeBaseInterface * node_base,
  const rosidl_message_type_support_t & type_support,
  const std::string & topic_name,
  const rclcpp::QoS & qos)
: PublisherBase(node_base, topic_name, type_support, publisher_options(qos))
{
}

void GenericPublisher::publish(const rclcpp::SerializedMessage & message)
{
  const auto result = rcl_publish_serialized_message(
    get_publisher_handle().get(), &message.get_rcl_serialized_message(), nullptr);
  if (result != RCL_RET_OK) {
    rclcpp::exceptions::throw_from_rcl_error(result, "failed to publish serialized message");
  }
}

LoadedTypeSupport load_typesupport(const std::string & type)
{
  std::string package;
  std::string module;
  std::string name;
  std::tie(package, module, name) = split_type(type);

  const auto prefix = ament_index_cpp::get_package_prefix(package);
  const auto library_path = prefix + "/lib/lib" + package + "__rosidl_typesupport_cpp.so";
  auto library = std::make_shared<rcpputils::SharedLibrary>(library_path);
  const auto symbol = "rosidl_typesupport_cpp__get_message_type_support_handle__" +
    package + "__" + module + "__" + name;

  auto raw_symbol = library->get_symbol(symbol);
  if (raw_symbol == nullptr) {
    throw std::runtime_error(
            "type support symbol '" + symbol + "' not found in " + library_path);
  }
  using GetHandle = const rosidl_message_type_support_t * (*)();
  const auto get_handle = reinterpret_cast<GetHandle>(raw_symbol);
  const auto * handle = get_handle();
  if (handle == nullptr) {
    throw std::runtime_error("type support returned null for '" + type + "'");
  }
  return LoadedTypeSupport{std::move(library), handle};
}

}  // namespace cdr_tcp_bridge

#endif
