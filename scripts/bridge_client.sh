#!/usr/bin/env bash
set -euo pipefail

# Offboard: isolate relayed topics from the robot's DDS network.
export ROS_DOMAIN_ID="${BRIDGE_ROS_DOMAIN_ID:-71}"
export ROS_LOCALHOST_ONLY=1
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

exec ros2 run cdr_tcp_bridge cdr_tcp_bridge --ros-args -p role:=client "$@"
