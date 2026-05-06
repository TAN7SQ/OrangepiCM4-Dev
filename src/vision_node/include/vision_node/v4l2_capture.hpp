#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// 对 Linux V4L2 摄像头采集流程做一个小封装：
// 打开设备、配置格式、申请 mmap 缓冲区、启动/停止视频流，并把 NV12 帧转成 OpenCV BGR 图像。
class V4L2Capture
{
public:
    // device 通常是 /dev/video0，width/height 是期望采集分辨率。
    V4L2Capture(const std::string &device, int width, int height);
    ~V4L2Capture();

    // 初始化摄像头和视频流，成功后才能 readFrame。
    bool openCamera();
    // 读取一帧图像，并转换为 OpenCV 常用的 BGR 格式。
    bool readFrame(cv::Mat &bgr_frame);
    // 停止视频流、释放 mmap 缓冲区并关闭 fd。
    void closeCamera();

private:
    // V4L2 mmap 缓冲区描述，一块缓冲区对应驱动里的一帧缓存。
    struct Buffer
    {
        void *start = nullptr;
        size_t length = 0;
    };

    // 包装 ioctl，自动重试被信号中断的系统调用。
    int xioctl(unsigned long request, void *arg);
    // 设置摄像头输出格式。
    bool initDevice();
    // 申请并映射 V4L2 缓冲区。
    bool initMmap();
    // 打开视频流。
    bool startStream();
    // 关闭视频流。
    void stopStream();

private:
    std::string device_;
    int width_;
    int height_;
    int fd_ = -1;
    std::vector<Buffer> buffers_;
};
