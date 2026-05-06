cmake_minimum_required(VERSION 3.8)
project(vision_node)

# 生成 compile_commands.json，方便 clangd/VS Code 做跳转和补全。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ROS 2 Humble 的 rclcpp 以及 chrono_literals 等代码按 C++17 解析。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# GCC/Clang 下打开常用告警，尽早发现潜在的 C++ 写法问题。
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ROS 2 和图像处理相关依赖。
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED)

include_directories(include)

# 主视觉节点：ROS 2 节点逻辑 + V4L2 摄像头采集封装。
add_executable(vision_node
  src/vision_node.cpp
  src/v4l2_capture.cpp
)

# 声明 ROS 2 依赖，ament 会自动补充 include 路径和链接参数。
ament_target_dependencies(vision_node
  rclcpp
  sensor_msgs
  geometry_msgs
  std_msgs
  cv_bridge
  image_transport
)

# OpenCV 不是 ament 包，这里单独链接 OpenCV 库。
target_link_libraries(vision_node
  ${OpenCV_LIBS}
)

# 安装后可通过 `ros2 run vision_node vision_node` 启动。
install(TARGETS
  vision_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
