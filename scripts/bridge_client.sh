#!/usr/bin/env bash
set -euo pipefail

# Offboard: isolate relayed topics from the robot's DDS network.
export ROS_DOMAIN_ID="${BRIDGE_ROS_DOMAIN_ID:-71}"
# Avoid selecting loopback twice when setup_local.sh already supplied a
# CycloneDDS interface. ROS_LOCALHOST_ONLY owns the client's DDS interface.
unset CYCLONEDDS_URI
export ROS_LOCALHOST_ONLY=1
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"

exec ros2 run cdr_tcp_bridge cdr_tcp_bridge --ros-args \
  -p role:=client \
  -p server_address:="${VIBE_BRIDGE_ADDRESS:-gilfoyle-rth}" \
  "$@"
