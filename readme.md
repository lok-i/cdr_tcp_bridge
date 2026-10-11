# cdr_tcp_bridge

Latest-only CDR telemetry over TCP. Foxy is supported onboard; Humble is
supported offboard. Messages are forwarded without deserialization.

## Build

```bash
source /opt/ros/foxy/setup.bash  # use humble offboard
cd cyclonedds_ws
colcon build --packages-select cdr_tcp_bridge
source install/setup.bash
```

## Run

Defaults: Orin `gilfoyle-rth` (`192.168.0.177`), TCP `5556`,
`config/full.yaml`. IPv4 addresses and `/etc/hosts`/DNS names are accepted.

The repository setup selects `gilfoyle-rth` for a real experiment and
`127.0.0.1` for simulation. Onboard, keep the same ROS domain as the
encoder/controller:

```bash
ros2 run cdr_tcp_bridge bridge_server.sh
```

Offboard, use an isolated local ROS domain:

```bash
ros2 run cdr_tcp_bridge bridge_client.sh
```

Use another profile when needed:

```bash
ros2 run cdr_tcp_bridge bridge_client.sh \
  -p config_path:="$(ros2 pkg prefix cdr_tcp_bridge)/share/cdr_tcp_bridge/config/light.yaml"
```

- `full.yaml`: frame, tokens, attention, low state, low command, and the planner's
  three state topics at up to 50 Hz.
- `light.yaml`: frame, tokens, attention, and the planner at up to 10 Hz.
- `no_latents.yaml`: frame and attention only.

The the planner channels are silent unless a planner is running, so `full` and `light`
carry them unconditionally — a closed-loop run needs no profile switch.

Server and client must use the same profile.

## Record offboard

```bash
export BRIDGE_SHARE="$(ros2 pkg prefix cdr_tcp_bridge)/share/cdr_tcp_bridge"
ros2 bag record -s mcap \
  --qos-profile-overrides-path "$BRIDGE_SHARE/config/record_qos.yaml" \
  --storage-config-file "$BRIDGE_SHARE/config/mcap_writer.yaml" \
  /enc/frame /enc/tokens /vibe/sonic/attention_mask /lowstate /lowcmd \
  /vibe/planner/status /vibe/controller/status /vibe/sonic/goal_color
```

The raw high-resolution JPEG stream remains on TCP port `5555`; it is not in
this bag yet.
