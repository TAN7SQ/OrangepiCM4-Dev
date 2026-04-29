#include <fcntl.h>
// video2提供视频设备的控制接口
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct CameraInfo
{
    // 默认使用单平面API，如果驱动报告支持则切换到多平面API
    v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool is_mplane = false;
};

static std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// 不区分大小写的子字符串匹配，用于模糊查找控制名称
static bool contains_ignore_case(const std::string &text, const std::string &key)
{
    return to_lower(text).find(to_lower(key)) != std::string::npos;
}

// 将4字符的像素格式名称（如NV12）转换为V4L2 fourcc值
static uint32_t fourcc_from_string(const std::string &s)
{
    if (s.size() != 4) {
        std::cerr << "Pixel format must be 4 characters, for example NV12, YUYV, MJPG\n";
        std::exit(1);
    }

    return v4l2_fourcc(s[0], s[1], s[2], s[3]);
}

// 将V4L2 fourcc代码转换回可读的4字符字符串
static std::string fourcc_to_string(uint32_t fcc)
{
    char str[5] = {static_cast<char>(fcc & 0xff),
                   static_cast<char>((fcc >> 8) & 0xff),
                   static_cast<char>((fcc >> 16) & 0xff),
                   static_cast<char>((fcc >> 24) & 0xff),
                   0};

    return std::string(str);
}

// 探测设备并确定其使用单平面还是多平面捕获API
static bool query_camera_info(int fd, CameraInfo &info)
{
    v4l2_capability cap{};

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP failed");
        return false;
    }

    std::cout << "Driver      : " << cap.driver << "\n";
    std::cout << "Card        : " << cap.card << "\n";
    std::cout << "Bus info    : " << cap.bus_info << "\n";
    std::cout << "Capabilities: 0x" << std::hex << cap.capabilities << std::dec << "\n";
    std::cout << "Device caps : 0x" << std::hex << cap.device_caps << std::dec << "\n";

    uint32_t caps = cap.device_caps ? cap.device_caps : cap.capabilities;

    if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        info.buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        info.is_mplane = true;
        std::cout << "Buffer type : VIDEO_CAPTURE_MPLANE\n";
    }
    else if (caps & V4L2_CAP_VIDEO_CAPTURE) {
        info.buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        info.is_mplane = false;
        std::cout << "Buffer type : VIDEO_CAPTURE\n";
    }
    else {
        std::cerr << "This device does not look like a video capture device.\n";
        return false;
    }

    return true;
}

// 在已打开的视频节点上设置新的宽度/高度/像素格式
static bool set_format(int fd, const CameraInfo &info, int width, int height, const std::string &pixfmt)
{
    v4l2_format fmt{};
    fmt.type = info.buffer_type;

    uint32_t fcc = fourcc_from_string(pixfmt);

    if (info.is_mplane) {
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = fcc;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    }
    else {
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = fcc;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT failed");
        return false;
    }

    if (info.is_mplane) {
        std::cout << "Actual format: " << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height << " "
                  << fourcc_to_string(fmt.fmt.pix_mp.pixelformat) << "\n";
    }
    else {
        std::cout << "Actual format: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << " "
                  << fourcc_to_string(fmt.fmt.pix.pixelformat) << "\n";
    }

    return true;
}

// 读取并打印当前活动的捕获格式
static bool get_format(int fd, const CameraInfo &info)
{
    v4l2_format fmt{};
    fmt.type = info.buffer_type;

    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT failed");
        return false;
    }

    if (info.is_mplane) {
        std::cout << "Current format: " << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height << " "
                  << fourcc_to_string(fmt.fmt.pix_mp.pixelformat) << "\n";
    }
    else {
        std::cout << "Current format: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << " "
                  << fourcc_to_string(fmt.fmt.pix.pixelformat) << "\n";
    }

    return true;
}

// 请求帧间隔并显示驱动接受的实际值
static bool set_fps(int fd, const CameraInfo &info, int fps)
{
    v4l2_streamparm parm{};
    parm.type = info.buffer_type;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        perror("VIDIOC_S_PARM failed");
        return false;
    }

    std::cout << "Requested FPS: " << fps << "\n";
    std::cout << "Actual timeperframe: " << parm.parm.capture.timeperframe.numerator << "/"
              << parm.parm.capture.timeperframe.denominator << "\n";

    if (parm.parm.capture.timeperframe.numerator != 0) {
        double actual_fps = static_cast<double>(parm.parm.capture.timeperframe.denominator) /
                            static_cast<double>(parm.parm.capture.timeperframe.numerator);

        std::cout << "Actual FPS approx: " << actual_fps << "\n";
    }

    return true;
}

// 枚举此视频节点公开的所有已启用控件
static void list_controls(int fd)
{
    std::cout << "Available controls:\n";

    v4l2_query_ext_ctrl query{};
    query.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    bool found = false;

    while (ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query) == 0) {
        if (!(query.flags & V4L2_CTRL_FLAG_DISABLED)) {
            found = true;

            std::cout << "  id=0x" << std::hex << query.id << std::dec << " name=\"" << query.name << "\""
                      << " type=" << query.type << " min=" << query.minimum << " max=" << query.maximum
                      << " step=" << query.step << " default=" << query.default_value << "\n";
        }

        query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    if (!found) {
        std::cout << "  No controls found on this video node.\n";
        std::cout << "  This can happen if controls are exposed on a subdev node such as /dev/v4l-subdevX.\n";
    }
}

// 读取名称与请求关键字模糊匹配的第一个控件
static bool get_control_by_name(int fd, const std::string &name_key)
{
    v4l2_query_ext_ctrl query{};
    query.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query) == 0) {
        if (!(query.flags & V4L2_CTRL_FLAG_DISABLED)) {
            std::string ctrl_name(reinterpret_cast<const char *>(query.name));

            if (contains_ignore_case(ctrl_name, name_key)) {
                v4l2_control ctrl{};
                ctrl.id = query.id;

                if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
                    std::cerr << "Failed to get control: " << ctrl_name << "\n";
                    perror("VIDIOC_G_CTRL failed");
                    return false;
                }

                std::cout << "Get control: " << ctrl_name << " = " << ctrl.value << "\n";
                return true;
            }
        }

        query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    std::cerr << "Control not found: " << name_key << "\n";
    return false;
}

// 设置名称与请求关键字模糊匹配的第一个控件
static bool set_control_by_name(int fd, const std::string &name_key, int value)
{
    v4l2_query_ext_ctrl query{};
    query.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &query) == 0) {
        if (!(query.flags & V4L2_CTRL_FLAG_DISABLED)) {
            std::string ctrl_name(reinterpret_cast<const char *>(query.name));

            if (contains_ignore_case(ctrl_name, name_key)) {
                if (value < query.minimum || value > query.maximum) {
                    std::cerr << "Warning: value " << value << " is outside range [" << query.minimum << ", "
                              << query.maximum << "] for " << ctrl_name << "\n";
                }

                v4l2_control ctrl{};
                ctrl.id = query.id;
                ctrl.value = value;

                if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                    std::cerr << "Failed to set control: " << ctrl_name << " = " << value << "\n";
                    perror("VIDIOC_S_CTRL failed");
                    return false;
                }

                std::cout << "Set control: " << ctrl_name << " = " << value << "\n";
                return true;
            }
        }

        query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    std::cerr << "Control not found: " << name_key << "\n";
    return false;
}

static void print_usage(const char *prog)
{
    std::cout << "Usage:\n"
              << "  " << prog << " --dev /dev/video0 --info\n"
              << "  " << prog << " --dev /dev/video0 --list-ctrls\n"
              << "  " << prog << " --dev /dev/video0 --get-fmt\n"
              << "  " << prog << " --dev /dev/video0 --width 1280 --height 720 --fps 30 --fmt NV12\n"
              << "  " << prog << " --dev /dev/video0 --ctrl exposure=800 --ctrl analogue=120\n"
              << "  " << prog << " --dev /dev/video0 --get-ctrl exposure\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " --dev /dev/video0 --info\n"
              << "  " << prog << " --dev /dev/video0 --list-ctrls\n"
              << "  " << prog << " --dev /dev/video0 --width 1280 --height 720 --fps 30 --fmt NV12\n"
              << "  " << prog << " --dev /dev/video0 --ctrl exposure=800\n"
              << "  " << prog << " --dev /dev/video0 --ctrl analogue=120\n";
}

int main(int argc, char **argv)
{
    // 首先收集命令行参数，然后按固定顺序应用请求的操作
    std::string dev = "/dev/video0";
    int width = -1;
    int height = -1;
    int fps = -1;
    std::string fmt;

    bool do_info = false;
    bool do_list_ctrls = false;
    bool do_get_fmt = false;

    std::vector<std::pair<std::string, int>> set_controls;
    std::vector<std::string> get_controls;

    // 支持的操作：
    // - 查看设备信息/控件/当前格式
    // - 更改像素格式或帧率
    // - 读取或写入命名的V4L2控件
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--dev" && i + 1 < argc) {
            dev = argv[++i];
        }
        else if (arg == "--width" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
        }
        else if (arg == "--height" && i + 1 < argc) {
            height = std::stoi(argv[++i]);
        }
        else if (arg == "--fps" && i + 1 < argc) {
            fps = std::stoi(argv[++i]);
        }
        else if (arg == "--fmt" && i + 1 < argc) {
            fmt = argv[++i];
        }
        else if (arg == "--info") {
            do_info = true;
        }
        else if (arg == "--list-ctrls") {
            do_list_ctrls = true;
        }
        else if (arg == "--get-fmt") {
            do_get_fmt = true;
        }
        else if (arg == "--get-ctrl" && i + 1 < argc) {
            get_controls.emplace_back(argv[++i]);
        }
        else if (arg == "--ctrl" && i + 1 < argc) {
            std::string kv = argv[++i];
            auto pos = kv.find('=');

            if (pos == std::string::npos) {
                std::cerr << "Invalid --ctrl format, expected name=value\n";
                return 1;
            }

            std::string key = kv.substr(0, pos);
            int value = std::stoi(kv.substr(pos + 1));
            set_controls.emplace_back(key, value);
        }
        else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        perror(("Failed to open " + dev).c_str());
        return 1;
    }

    std::cout << "Opened device: " << dev << "\n";

    CameraInfo info;
    if (!query_camera_info(fd, info)) {
        close(fd);
        return 1;
    }

    if (do_info) {
        // query_camera_info已经打印了基本信息
    }

    if (do_list_ctrls) {
        list_controls(fd);
    }

    if (do_get_fmt) {
        get_format(fd, info);
    }

    // 格式更改只有在同时提供width、height和fmt三个参数时才有效
    if (width > 0 && height > 0 && !fmt.empty()) {
        if (!set_format(fd, info, width, height, fmt)) {
            close(fd);
            return 1;
        }
    }
    else if (width > 0 || height > 0 || !fmt.empty()) {
        std::cerr << "To set format, --width, --height and --fmt must be provided together.\n";
        close(fd);
        return 1;
    }

    if (fps > 0) {
        if (!set_fps(fd, info, fps)) {
            close(fd);
            return 1;
        }
    }

    // 在读取之前先应用写入操作，以便调用者可以在单次运行中设置并验证值
    for (const auto &item : set_controls) {
        set_control_by_name(fd, item.first, item.second);
    }

    for (const auto &item : get_controls) {
        get_control_by_name(fd, item);
    }

    close(fd);
    return 0;
}