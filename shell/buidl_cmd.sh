cd ~/App/PinyDart
rm -rf build install log
colcon build --symlink-install
source install/setup.bash
ros2 run vision_node vision_node