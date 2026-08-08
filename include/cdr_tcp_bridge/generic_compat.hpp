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

#pragma once

#if !defined(CDR_TCP_BRIDGE_FORCE_FOXY_COMPAT) && \
  __has_include(<rclcpp/generic_publisher.hpp>) && \
  __has_include(<rclcpp/generic_subscription.hpp>)
#define CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC 1

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/generic_subscription.hpp>

namespace cdr_tcp_bridge
{

using GenericPublisher = rclcpp::GenericPublisher;
using GenericSubscription = rclcpp::GenericSubscription;

}  // namespace cdr_tcp_bridge

#else
#define CDR_TCP_BRIDGE_HAS_NATIVE_GENERIC 0

#include <rclcpp/macros.hpp>
#include <rclcpp/publisher_base.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rclcpp/subscription.hpp>
#include <rcpputils/shared_library.hpp>
#include <rosidl_runtime_cpp/message_type_support_decl.hpp>

#include <functional>
#include <memory>
#include <string>

namespace cdr_tcp_bridge
{

// Foxy predates rclcpp::GenericSubscription and rclcpp::GenericPublisher.
// These minimal equivalents use the same serialized rcl/rmw API used by
// Foxy's rosbag2 transport, keeping the bridge payload as raw CDR.
class GenericSubscription final : public rclcpp::SubscriptionBase
{
public:
  using SharedPtr = std::shared_ptr<GenericSubscription>;

  GenericSubscription(
    rclcpp::node_interfaces::NodeBaseInterface * node_base,
    const rosidl_message_type_support_t & type_support,
    const std::string & topic_name,
    const rclcpp::QoS & qos,
    std::function<void(std::shared_ptr<rclcpp::SerializedMessage>)> callback);

  std::shared_ptr<void> create_message() override;
  std::shared_ptr<rclcpp::SerializedMessage> create_serialized_message() override;
  void handle_message(
    std::shared_ptr<void> & message, const rclcpp::MessageInfo & message_info) override;
  // This is a pure virtual in newer rclcpp and absent from Foxy's base class.
  // Omitting `override` lets the same compatibility source compile against both.
  void handle_serialized_message(
    const std::shared_ptr<rclcpp::SerializedMessage> & message,
    const rclcpp::MessageInfo & message_info);
  void handle_loaned_message(
    void * loaned_message, const rclcpp::MessageInfo & message_info) override;
  void return_message(std::shared_ptr<void> & message) override;
  void return_serialized_message(
    std::shared_ptr<rclcpp::SerializedMessage> & message) override;

private:
  RCLCPP_DISABLE_COPY(GenericSubscription)
  std::function<void(std::shared_ptr<rclcpp::SerializedMessage>)> callback_;
};

class GenericPublisher final : public rclcpp::PublisherBase
{
public:
  using SharedPtr = std::shared_ptr<GenericPublisher>;

  GenericPublisher(
    rclcpp::node_interfaces::NodeBaseInterface * node_base,
    const rosidl_message_type_support_t & type_support,
    const std::string & topic_name,
    const rclcpp::QoS & qos);

  void publish(const rclcpp::SerializedMessage & message);
};

struct LoadedTypeSupport
{
  std::shared_ptr<rcpputils::SharedLibrary> library;
  const rosidl_message_type_support_t * handle = nullptr;
};

LoadedTypeSupport load_typesupport(const std::string & type);

}  // namespace cdr_tcp_bridge

#endif
