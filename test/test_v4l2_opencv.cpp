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

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct Buffer
{
    void *start = nullptr;
    size_t length = 0;
};

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
    const char *dev_name = "/dev/video0";
    const int width = 1920;
    const int height = 1080;

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

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(fd);
            return -1;
        }

        buffers[i].length = buf.m.planes[0].length;
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

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            close(fd);
            return -1;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        close(fd);
        return -1;
    }

    const int frame_count = 120;
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < frame_count; ++i) {
        v4l2_buffer buf;
        v4l2_plane planes[VIDEO_MAX_PLANES];
        CLEAR(buf);
        CLEAR(planes);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            break;
        }

        cv::Mat nv12(height * 3 / 2, width, CV_8UC1, buffers[buf.index].start);
        cv::Mat bgr;
        cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);

        if (i == 30) {
            cv::imwrite("v4l2_opencv_bgr.jpg", bgr);

            cv::Mat hsv, mask;
            cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            cv::inRange(hsv, cv::Scalar(35, 60, 60), cv::Scalar(85, 255, 255), mask);
            cv::imwrite("v4l2_opencv_green_mask.jpg", mask);

            std::cout << "Saved v4l2_opencv_bgr.jpg and v4l2_opencv_green_mask.jpg" << std::endl;
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF return");
            break;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "Average FPS: " << frame_count / seconds << std::endl;

    xioctl(fd, VIDIOC_STREAMOFF, &type);

    for (auto &b : buffers) {
        if (b.start && b.start != MAP_FAILED) {
            munmap(b.start, b.length);
        }
    }

    close(fd);
    return 0;
}