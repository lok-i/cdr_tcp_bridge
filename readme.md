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

Defaults: Orin `192.168.0.177`, TCP `5556`, `config/full.yaml`.

Onboard, keep the same ROS domain as the encoder/controller:

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

- `full.yaml`: frame, tokens, attention at up to 50 Hz.
- `light.yaml`: the same topics at up to 10 Hz.
- `no_latents.yaml`: frame and attention only.

Server and client must use the same profile.

## Record offboard

```bash
export BRIDGE_SHARE="$(ros2 pkg prefix cdr_tcp_bridge)/share/cdr_tcp_bridge"
ros2 bag record -s mcap \
  --qos-profile-overrides-path "$BRIDGE_SHARE/config/record_qos.yaml" \
  --storage-config-file "$BRIDGE_SHARE/config/mcap_writer.yaml" \
  /enc/frame /enc/tokens /vibe/sonic/attention_mask
```

The raw high-resolution JPEG stream remains on TCP port `5555`; it is not in
this bag yet.
