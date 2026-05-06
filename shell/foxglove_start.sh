# 加载系统 ROS 2 Humble 环境。
source /opt/ros/humble/setup.bash

# 加载 PinyDart 工作区里的 vision_node 包环境。
source ~/App/PinyDart/install/setup.bash

# 启动 Foxglove Bridge，把 ROS 2 topic 暴露给 Foxglove Studio 观察。
ros2 launch foxglove_bridge foxglove_bridge_launch.xml
