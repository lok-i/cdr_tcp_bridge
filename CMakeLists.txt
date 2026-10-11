cmake_minimum_required(VERSION 3.8)
project(cdr_tcp_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rcpputils REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosidl_runtime_cpp REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(cdr_tcp_bridge_core
  src/config.cpp
  src/protocol.cpp
)
target_compile_features(cdr_tcp_bridge_core PUBLIC cxx_std_17)
target_include_directories(cdr_tcp_bridge_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(cdr_tcp_bridge_core yaml-cpp)

add_executable(cdr_tcp_bridge src/bridge.cpp)
target_sources(cdr_tcp_bridge PRIVATE src/generic_compat.cpp)
target_compile_features(cdr_tcp_bridge PUBLIC cxx_std_17)
target_link_libraries(cdr_tcp_bridge cdr_tcp_bridge_core)
ament_target_dependencies(
  cdr_tcp_bridge
  ament_index_cpp
  rcpputils
  rclcpp
  rosidl_runtime_cpp)

install(TARGETS cdr_tcp_bridge_core cdr_tcp_bridge
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(PROGRAMS
  scripts/bridge_server.sh
  scripts/bridge_client.sh
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_protocol test/test_protocol.cpp)
  target_link_libraries(test_protocol cdr_tcp_bridge_core)

  ament_add_gtest(test_latest_slot test/test_latest_slot.cpp)
  target_include_directories(test_latest_slot PRIVATE include)

  ament_add_gtest(test_config test/test_config.cpp)
  target_link_libraries(test_config cdr_tcp_bridge_core)
  target_compile_definitions(test_config PRIVATE
    TEST_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/channels.yaml"
    TEST_REPOSE_CALIB_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/repose_calibration.yaml")
endif()

ament_export_include_directories(include)
ament_export_libraries(cdr_tcp_bridge_core)
ament_package()
