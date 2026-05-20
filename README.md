# PinyDart

PinyDart 是运行在 Orange Pi CM4 上的 ROS 2 Humble 视觉工作区。当前核心包是 `vision_node`：它通过 Linux V4L2 直接读取 `/dev/video0` 摄像头的 NV12 图像，转换为 OpenCV BGR 图像后做绿色目标检测，并把识别结果发布成 ROS 2 topic，方便后续控制节点或 Foxglove Studio 调试。

## 项目结构

```text
/home/orangepi/App/PinyDart
├── shell/
│   ├── buidl_cmd.sh              # 干净构建工作区，并启动 vision_node
│   └── foxglove_start.sh         # 启动 Foxglove Bridge，给 Foxglove Studio 看 topic
├── src/
│   └── vision_node/
│       ├── CMakeLists.txt        # ROS 2 C++ 包构建配置
│       ├── package.xml           # ROS 2 包依赖声明
│       ├── include/
│       │   └── vision_node/
│       │       └── v4l2_capture.hpp
│       └── src/
│           ├── vision_node.cpp   # ROS 2 节点：检测绿色目标并发布 topic
│           └── v4l2_capture.cpp  # V4L2 摄像头初始化、mmap 采集、NV12 转 BGR
├── test/
│   └── test_v4l2_opencv.cpp      # 不经过 ROS 2 的 V4L2/OpenCV 采集测试代码
├── .clangd                       # clangd/VS Code 代码补全和跳转配置
├── .clang-format                 # C++ 代码格式配置
├── .gitignore                    # 忽略 build、install、log 等生成目录
└── compile_commands.json         # 指向 build/vision_node/compile_commands.json 的符号链接
```

生成目录说明：

- `build/`：`colcon build` 的编译中间产物。
- `install/`：ROS 2 包安装结果，运行节点前需要 `source install/setup.bash`。
- `log/`：colcon 构建日志。
- `.cache/`：本地工具缓存。

这些目录都是生成物，通常不需要提交到 git。

## 代码工作流程

`vision_node` 启动后会做以下事情：

1. 打开 `/dev/video0`，并通过 `v4l2-ctl` 设置摄像头链路、分辨率、NV12 格式、曝光和增益。
2. 用 V4L2 `mmap` 方式申请 4 个缓冲区，循环从驱动取帧。
3. 将摄像头输出的 NV12 图像转换为 OpenCV BGR 图像。
4. 将 BGR 图像转换为 HSV，用绿色阈值 `H=35~85, S>=60, V>=60` 分割绿色目标。
5. 找最大绿色轮廓，用轮廓矩计算目标中心点，并过滤面积小于 `50` 的噪声。
6. 发布目标状态、目标中心点、调试压缩图和 FPS。

主要 ROS 2 topic：

| Topic | 类型 | 作用 |
| --- | --- | --- |
| `/vision/target_found` | `std_msgs/msg/Bool` | 是否检测到有效绿色目标 |
| `/vision/target_center` | `geometry_msgs/msg/PointStamped` | 目标中心像素坐标，`x/y` 是图像坐标，`z` 复用为目标轮廓面积 |
| `/vision/debug_image/compressed` | `sensor_msgs/msg/CompressedImage` | 压缩后的调试图，给 Foxglove 观察识别效果 |
| `/vision/fps` | `std_msgs/msg/Float32` | 节点实际处理帧率 |

调试图效果：

- 白点表示画面中心。
- 检测到目标时，红点表示绿色目标中心，蓝线表示画面中心到目标中心的偏移。
- 未检测到目标时，图像左上角显示 `target lost`。
- 调试图会缩放到 `640x360`，并以 JPEG 质量 `60` 发布，降低网络带宽和编码开销。

## shell 脚本使用方法

脚本都在 `shell/` 目录下，并且当前已经有可执行权限。

### 1. 构建并启动视觉节点

脚本名目前是 `buidl_cmd.sh`，注意这里的拼写是项目中的实际文件名。

```bash
cd ~/App/PinyDart

# 如果当前终端还没有加载 ROS 2 Humble，先执行：
source /opt/ros/humble/setup.bash

./shell/buidl_cmd.sh
```

这个脚本会依次执行：

```bash
cd ~/App/PinyDart
rm -rf build install log
colcon build --symlink-install
source install/setup.bash
ros2 run vision_node vision_node
```

效果：

- 删除旧的 `build/`、`install/`、`log/`，保证这次是干净构建。
- 使用 `colcon build --symlink-install` 编译 `vision_node`。
- 加载当前工作区环境。
- 启动 `vision_node`。
- 节点启动成功后，会持续读取摄像头、检测绿色目标，并发布 `/vision/*` topic。

运行时可以另开一个终端检查：

```bash
source /opt/ros/humble/setup.bash
source ~/App/PinyDart/install/setup.bash

ros2 topic list
ros2 topic echo /vision/target_found
ros2 topic echo /vision/fps
```

### 2. 启动 Foxglove Bridge

视觉节点运行后，另开一个终端执行：

```bash
cd ~/App/PinyDart
./shell/foxglove_start.sh
```

这个脚本会依次执行：

```bash
source /opt/ros/humble/setup.bash
source ~/App/PinyDart/install/setup.bash
ros2 launch foxglove_bridge foxglove_bridge_launch.xml
```

效果：

- 加载系统 ROS 2 Humble 环境。
- 加载 PinyDart 工作区里的 `vision_node` 包环境。
- 启动 `foxglove_bridge`。
- Foxglove Studio 可以通过 bridge 连接到板子，查看 `/vision/debug_image/compressed`、`/vision/target_center`、`/vision/target_found`、`/vision/fps` 等 topic。

## 单独测试摄像头采集

`test/test_v4l2_opencv.cpp` 是一个不经过 ROS 2 的 V4L2/OpenCV 测试程序，主要用于验证摄像头链路、NV12 转 BGR、绿色阈值 mask 和平均 FPS。

它的效果是：

- 直接打开 `/dev/video0`。
- 配置 `1920x1080`、`NV12`、曝光和增益。
- 连续采集 120 帧并计算平均 FPS。
- 在第 30 帧保存：
  - `v4l2_opencv_bgr.jpg`
  - `v4l2_opencv_green_mask.jpg`

注意：这个测试文件当前没有注册到 `src/vision_node/CMakeLists.txt`，所以 `colcon build` 默认不会生成这个测试可执行文件。如果要使用它，可以临时写一个独立编译命令，或者把它加入 CMake 后再构建。

## 常见问题

### 摄像头打不开

先检查设备是否存在：

```bash
ls -l /dev/video0 /dev/v4l-subdev0
```

再检查当前用户是否有访问摄像头设备的权限。如果 `vision_node` 报 `open camera`、`VIDIOC_S_FMT`、`VIDIOC_REQBUFS` 等错误，优先确认摄像头驱动、设备节点、格式设置和是否有其他程序占用摄像头。

### Foxglove 看不到数据

先确认视觉节点已经在运行，并且 topic 存在：

```bash
source /opt/ros/humble/setup.bash
source ~/App/PinyDart/install/setup.bash
ros2 topic list
```

如果 `/vision/debug_image/compressed` 存在但 Foxglove 没有画面，再检查 `foxglove_bridge` 是否启动、电脑和 Orange Pi 是否在同一网络，以及 Foxglove Studio 连接的 IP 和端口是否正确。

### 修改代码后补全或跳转不正常

本工作区的 `compile_commands.json` 是指向 `build/vision_node/compile_commands.json` 的符号链接。重新构建后，如果编辑器仍然不识别 include 或 C++ 标准，可以先确认：

```bash
ls -l ~/App/PinyDart/compile_commands.json
```

然后重启 clangd 或 VS Code Remote 窗口。
