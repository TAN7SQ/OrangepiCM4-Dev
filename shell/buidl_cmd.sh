# 进入 ROS 2 工作区根目录。
cd ~/App/PinyDart

# 清理旧的 colcon 构建产物，确保这次是干净构建。
rm -rf build install log

# 构建工作区，--symlink-install 便于开发时修改资源文件后立即生效。
colcon build --symlink-install

# 加载当前工作区的 ROS 2 环境。
source install/setup.bash

# 启动视觉节点。
ros2 run vision_node vision_node
