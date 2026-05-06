#include "vision_node/v4l2_capture.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

V4L2Capture::V4L2Capture(const std::string &device, int width, int height)
    : device_(device), width_(width), height_(height)
{
}

V4L2Capture::~V4L2Capture()
{
    closeCamera();
}

int V4L2Capture::xioctl(unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd_, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

bool V4L2Capture::openCamera()
{

    system("v4l2-ctl -d /dev/v4l-subdev0 --set-subdev-selection pad=0,target=crop,left=0,top=0,width=1920,height=1080");
    system("v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=NV12");

    system("v4l2-ctl -d /dev/video0 --set-ctrl=exposure=1500");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=gain=512");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=analogue_gain=1024");

    fd_ = ::open(device_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror("open camera");
        return false;
    }

    if (!initDevice())
        return false;
    if (!initMmap())
        return false;
    if (!startStream())
        return false;

    return true;
}

bool V4L2Capture::initDevice()
{
    v4l2_format fmt;
    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;

    if (xioctl(VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return false;
    }

    std::cout << "Camera format: " << fmt.fmt.pix_mp.width << "x" << fmt.fmt.pix_mp.height
              << " NV12, sizeimage=" << fmt.fmt.pix_mp.plane_fmt[0].sizeimage << std::endl;

    return true;
}

bool V4L2Capture::initMmap()
{
    v4l2_requestbuffers req;
    CLEAR(req);

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return false;
    }

    buffers_.resize(req.count);

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

        if (xioctl(VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return false;
        }

        buffers_[i].length = buf.m.planes[0].length;
        buffers_[i].start =
            mmap(nullptr, buffers_[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.planes[0].m.mem_offset);

        if (buffers_[i].start == MAP_FAILED) {
            perror("mmap");
            return false;
        }
    }

    for (unsigned int i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf;
        v4l2_plane planes[VIDEO_MAX_PLANES];
        CLEAR(buf);
        CLEAR(planes);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        if (xioctl(VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return false;
        }
    }

    return true;
}

bool V4L2Capture::startStream()
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (xioctl(VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }

    return true;
}

void V4L2Capture::stopStream()
{
    if (fd_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        xioctl(VIDIOC_STREAMOFF, &type);
    }
}

bool V4L2Capture::readFrame(cv::Mat &bgr_frame)
{
    v4l2_buffer buf;
    v4l2_plane planes[VIDEO_MAX_PLANES];
    CLEAR(buf);
    CLEAR(planes);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    // DeQueue Buff,取出一个缓冲区
    if (xioctl(VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return false;
    }

    // 零拷贝构造，用Mat指针指向V4l2的内存
    /*
    以下是NV12的内存排布，总大小是  height * width * 1.5
    YYYYYYYYYYYY      ← Y（亮度）  height × width
    UVUVUVUVUVUV      ← UV（色度） height/2 × width
    */
    // 这里CV_8UC1是8位无符号单通道，以为NV12在内存中是“一整块字节流”
    cv::Mat nv12(height_ * 3 / 2, width_, CV_8UC1, buffers_[buf.index].start);
    // 摄像头输出NV12（YUV），Opencv要用BGR
    cv::cvtColor(nv12, bgr_frame, cv::COLOR_YUV2BGR_NV12);

    // 把buffer放回队列
    if (xioctl(VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF return");
        return false;
    }

    return true;
}

void V4L2Capture::closeCamera()
{
    stopStream();

    for (auto &b : buffers_) {
        if (b.start && b.start != MAP_FAILED) {
            munmap(b.start, b.length);
            b.start = nullptr;
        }
    }

    buffers_.clear();

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}