# cdr_tcp_bridge

Small, latest-only bridge for a fixed allowlist of serialized ROS 2 topics.
The server takes serialized CDR from ROS, wraps it in a versioned TCP frame,
and never deserializes it. The client republishes the same CDR into its local
ROS domain. Humble uses rclcpp's generic endpoints; Foxy uses the package's
small compatibility implementation of the same serialized rcl/rmw path.

This is telemetry, not a reliable queue: every channel has one pending slot.
A newer sample overwrites an unsent sample, a blocked socket is disconnected
after `send_timeout_ms`, and no network operation runs in a ROS callback.

## Build and test

```bash
cd cyclonedds_ws
source /opt/ros/foxy/setup.bash  # use /opt/ros/humble/setup.bash offboard
colcon build --packages-select cdr_tcp_bridge
colcon test --packages-select cdr_tcp_bridge
colcon test-result --verbose
source install/setup.bash
```

The package supports ROS 2 Foxy and Humble. Foxy does not provide
`rclcpp::GenericSubscription`/`GenericPublisher`, so the compatible endpoint
implementation is selected automatically at compile time. It dynamically
loads each configured message type's existing `rosidl_typesupport_cpp`
library; it does not add rosbag2 as an onboard dependency.

Both machines must have the message packages named in the channel config. Use
the same `RMW_IMPLEMENTATION` on both ends when possible; the deployed setup
uses CycloneDDS.

## Run

The server joins the onboard ROS domain and binds TCP only on the Orin's lab
Wi-Fi address:

```bash
source install/setup.bash
ros2 run cdr_tcp_bridge cdr_tcp_bridge --ros-args \
  -p role:=server \
  -p bind_address:=<orin_wlan0_ip> \
  -p port:=5556 \
  -p config_path:="$(ros2 pkg prefix cdr_tcp_bridge)/share/cdr_tcp_bridge/config/light.yaml"
```

The client uses a different, localhost-only ROS domain. TCP still connects over
lab Wi-Fi; `ROS_LOCALHOST_ONLY` applies only to its DDS side.

```bash
source install/setup.bash
export ROS_DOMAIN_ID=71
export ROS_LOCALHOST_ONLY=1
ros2 run cdr_tcp_bridge cdr_tcp_bridge --ros-args \
  -p role:=client \
  -p server_address:=<orin_wlan0_ip> \
  -p port:=5556 \
  -p config_path:="$(ros2 pkg prefix cdr_tcp_bridge)/share/cdr_tcp_bridge/config/light.yaml"
```

Do not set `ROS_LOCALHOST_ONLY=1` onboard when the controller needs the robot's
Ethernet DDS domain. Bind CycloneDDS to robot Ethernet and bind this TCP server
to lab Wi-Fi; they are independent interfaces.

## Profiles

- `light.yaml`: frame, tokens, and attention capped at 10 Hz for viewing.
- `full.yaml`: all three at 50 Hz for recording.
- `no_latents.yaml`: frame and attention only at 50 Hz.

Channel IDs and types are part of the wire contract, so the server and client
must use matching configs. Lower `priority` values are sent first. A zero
`max_rate_hz` means unlimited.

The current attention type is `std_msgs/msg/Float32MultiArray`, matching
`g1_vibe_sonic`. Replace it in both configs when a stamped attention message is
introduced.

## Offboard recording

The client republishes ordinary ROS topics, so use rosbag2 rather than writing a
custom recorder:

```bash
sudo apt install ros-humble-rosbag2-storage-mcap
export BRIDGE_SHARE="$(ros2 pkg prefix cdr_tcp_bridge)/share/cdr_tcp_bridge"

# Light dataset without latents
ros2 bag record -s mcap \
  --qos-profile-overrides-path "$BRIDGE_SHARE/config/record_qos.yaml" \
  --storage-config-file "$BRIDGE_SHARE/config/mcap_writer.yaml" \
  /enc/frame \
  /vibe/sonic/attention_mask

# Full dataset
ros2 bag record -s mcap \
  --qos-profile-overrides-path "$BRIDGE_SHARE/config/record_qos.yaml" \
  --storage-config-file "$BRIDGE_SHARE/config/mcap_writer.yaml" \
  /enc/frame \
  /enc/tokens \
  /vibe/sonic/attention_mask
```

The high-resolution camera remains on its existing JPEG TCP connection at port
5555. For a unified bag, an offboard camera-ingest process should publish the
original JPEG bytes as `sensor_msgs/msg/CompressedImage`; it should not decode
and re-encode them. Record that topic alongside the bridged topics.

MCAP is useful here because it preserves ROS schemas and timestamps beside
tokens and attention. JPEG is already compressed, so MCAP compression will not
substantially shrink the camera stream. For long video-only recordings, a
video container is smaller and easier to play, but it loses the convenient ROS
topic/timestamp dataset structure.

## Runtime parameters

| Parameter | Default | Meaning |
|---|---:|---|
| `role` | `server` | `server` onboard or `client` offboard |
| `config_path` | installed `light.yaml` | Shared channel map |
| `bind_address` | `127.0.0.1` | Server TCP bind address |
| `server_address` | `127.0.0.1` | Client TCP destination |
| `port` | `5556` | TCP port |
| `send_timeout_ms` | `20` | Maximum sender-thread stall before disconnect |
| `receive_timeout_ms` | `1000` | Idle poll interval; idle does not disconnect |
| `reconnect_ms` | `1000` | Client reconnect interval |
| `socket_buffer_bytes` | `262144` | Requested TCP send-buffer size |
| `max_payload_bytes` | `8388608` | Malformed/oversize frame guard |

Every five seconds the server reports received, sent, overwritten, and
oversize counts per topic. Overwrites are expected under bandwidth pressure and
are the mechanism that prevents telemetry backpressure from growing onboard.

## Protocol

Each frame starts with a 32-byte, network-byte-order header:

```text
magic:u32  version:u16  header_size:u16
channel:u16  flags:u16  payload_size:u32
sequence:u64  send_timestamp_ns:u64
payload:payload_size bytes of ROS CDR
```

The protocol layer itself has no ROS dependency. A future native client can
consume these frames directly, but it still needs the configured ROS type
schema to interpret each CDR payload.
