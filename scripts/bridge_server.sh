#!/usr/bin/env bash
set -euo pipefail

# Onboard: source the robot workspace first. Keep its existing ROS domain.
exec ros2 run cdr_tcp_bridge cdr_tcp_bridge --ros-args -p role:=server "$@"
