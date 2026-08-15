#!/usr/bin/env bash
set -euo pipefail

# Keep the experiment's ROS domain; setup.sh/setup_local.sh selects the TCP bind host.
exec ros2 run cdr_tcp_bridge cdr_tcp_bridge --ros-args \
  -p role:=server \
  -p bind_address:="${VIBE_BRIDGE_ADDRESS:-gilfoyle-rth}" \
  "$@"
