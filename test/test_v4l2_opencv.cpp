#include <opencv2/opencv.hpp>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

// 清零 V4L2 结构体，避免未初始化字段影响 ioctl 行为。
#define CLEAR(x) memset(&(x), 0, sizeof(x))

// mmap 后的 V4L2 缓冲区信息。
struct Buffer
{
    void *start = nullptr;
    size_t length = 0;
};

// ioctl 可能被信号打断并返回 EINTR，这里统一重试。
static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int main()
{
    // 测试程序直接使用 /dev/video0，不经过 ROS 2。
    const char *dev_name = "/dev/video0";
    const int width = 1920;
    const int height = 1080;

    // 配置摄像头链路、输出格式和曝光增益，便于单独验证采集效果。
    system("v4l2-ctl -d /dev/v4l-subdev0 --set-subdev-selection pad=0,target=crop,left=0,top=0,width=1920,height=1080");
    system("v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=NV12");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=exposure=3000");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=gain=1024");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=analogue_gain=1024");

    int fd = open(dev_name, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    v4l2_format fmt;
    CLEAR(fmt);
    // 使用多平面 V4L2 采集类型，图像格式为 NV12。
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width;
    fmt.fmt.pix_mp.height = height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        close(fd);
        return -1;
    }

    std::cout << "Format set: " << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height
              << " sizeimage=" << fmt.fmt.pix_mp.plane_fmt[0].sizeimage << std::endl;

    v4l2_requestbuffers req;
    CLEAR(req);
    // 申请 4 个 mmap 缓冲区，让驱动和用户态可以循环交替使用。
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return -1;
    }

    std::vector<Buffer> buffers(req.count);

    for (unsigned int i = 0; i < req.count; ++i) {
        v4l2_buffer buf;
        v4l2_plane planes[VIDEO_MAX_PLANES];
        CLEAR(buf);
        CLEAR(planes);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        // 查询缓冲区长度和 mmap 偏移。
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(fd);
            return -1;
        }

        buffers[i].length = buf.m.planes[0].length;
        // 映射驱动缓冲区到用户态地址，后续可直接读取图像数据。
        buffers[i].start =
            mmap(nullptr, buffers[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.planes[0].m.mem_offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return -1;
        }
    }

    for (unsigned int i = 0; i < req.count; ++i) {
        v4l2_buffer buf;
        v4l2_plane planes[VIDEO_MAX_PLANES];
        CLEAR(buf);
        CLEAR(planes);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        // 把空缓冲区放入驱动队列，等待摄像头写入帧数据。
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            close(fd);
            return -1;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    // 开始采集视频流。
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        close(fd);
        return -1;
    }

    const int frame_count = 120;
    auto t0 = std::chrono::steady_clock::now();

    // 连续采集 120 帧，用于计算平均 FPS。
    for (int i = 0; i < frame_count; ++i) {
        v4l2_buffer buf;
        v4l2_plane planes[VIDEO_MAX_PLANES];
        CLEAR(buf);
        CLEAR(planes);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        // 取出一帧已采集完成的缓冲区。
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            break;
        }

        // NV12 高度是原图的 1.5 倍，转换成 OpenCV 常用 BGR 图像。
        cv::Mat nv12(height * 3 / 2, width, CV_8UC1, buffers[buf.index].start);
        cv::Mat bgr;
        cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);

        if (i == 30) {
            // 保存一帧原始 BGR 图和绿色阈值 mask，便于离线查看识别效果。
            cv::imwrite("v4l2_opencv_bgr.jpg", bgr);

            cv::Mat hsv, mask;
            cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            cv::inRange(hsv, cv::Scalar(35, 60, 60), cv::Scalar(85, 255, 255), mask);
            cv::imwrite("v4l2_opencv_green_mask.jpg", mask);

            std::cout << "Saved v4l2_opencv_bgr.jpg and v4l2_opencv_green_mask.jpg" << std::endl;
        }

        // 处理完当前帧后，把缓冲区还给驱动继续采集。
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF return");
            break;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();
    // 用采集帧数除以耗时，估算摄像头采集和颜色转换整体 FPS。
    std::cout << "Average FPS: " << frame_count / seconds << std::endl;

    // 停止视频流，并释放 mmap 缓冲区和设备 fd。
    xioctl(fd, VIDIOC_STREAMOFF, &type);

    for (auto &b : buffers) {
        if (b.start && b.start != MAP_FAILED) {
            munmap(b.start, b.length);
        }
    }

    close(fd);
    return 0;
}
