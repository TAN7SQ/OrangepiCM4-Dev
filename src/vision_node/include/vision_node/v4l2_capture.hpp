#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class V4L2Capture
{
public:
    V4L2Capture(const std::string &device, int width, int height);
    ~V4L2Capture();

    bool openCamera();
    bool readFrame(cv::Mat &bgr_frame);
    void closeCamera();

private:
    struct Buffer
    {
        void *start = nullptr;
        size_t length = 0;
    };

    int xioctl(unsigned long request, void *arg);
    bool initDevice();
    bool initMmap();
    bool startStream();
    void stopStream();

private:
    std::string device_;
    int width_;
    int height_;
    int fd_ = -1;
    std::vector<Buffer> buffers_;
};