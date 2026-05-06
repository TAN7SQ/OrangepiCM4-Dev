#include "vision_node/v4l2_capture.hpp"

#include <geometry_msgs/msg/point_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32.hpp>

#include <chrono>
// #include <functional> // std::bind，把函数+对象打包成一个可调用的对象，相当于lambda函数
#include <iostream>
#include <vector>

using namespace std::chrono_literals;

// ROS 2 视觉节点：
// 1. 从 V4L2Capture 获取摄像头 BGR 图像；
// 2. 用 HSV 阈值寻找绿色目标；
// 3. 发布目标是否存在、目标中心、调试压缩图和 FPS。
class VisionNode : public rclcpp::Node
{
public:
    VisionNode() : Node("vision_node"), camera_("/dev/video0", 1920, 1080)
    {
        // 图像类 topic 使用 SensorDataQoS：只保留最新帧，允许 best effort，降低延迟。
        auto sensor_qos = rclcpp::SensorDataQoS().keep_last(1).best_effort();

        // 调试图像：带目标点、画面中心、连线和文字信息的 JPEG 压缩图。
        // 目标中心：point.x/y 是像素坐标，point.z 复用为轮廓面积。
        // 是否检测到目标。
        // 节点实际处理帧率。
        debug_pub_ = this->create_publisher<sensor_msgs::msg::CompressedImage>( //
            "/vision/debug_image/compressed",
            sensor_qos);
        target_pub_ = this->create_publisher<geometry_msgs::msg::PointStamped>( //
            "/vision/target_center",
            1);
        found_pub_ = this->create_publisher<std_msgs::msg::Bool>( //
            "/vision/target_found",
            1);
        fps_pub_ = this->create_publisher<std_msgs::msg::Float32>( //
            "/vision/fps",
            1);

        // 启动摄像头，失败时直接抛异常让 main 捕获并退出。
        if (!camera_.openCamera()) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open V4L2 camera");
            throw std::runtime_error("camera open failed");
        }

        last_time_ = this->now();

        // 尽可能快地轮询摄像头；真正帧率由摄像头和处理耗时决定。
        timer_ = this->create_wall_timer(1ms, [this]() {
            this->timerCallback();
        });

        RCLCPP_INFO(this->get_logger(), "Vision node started");
    }

private:
    void timerCallback()
    {
        cv::Mat frame;

        // 读取一帧 BGR 图像；失败时跳过本次定时回调。
        if (!camera_.readFrame(frame)) {
            RCLCPP_WARN(this->get_logger(), "Failed to read frame");
            return;
        }

        auto now = this->now();

        bool found = false;
        cv::Point2f center(0.0f, 0.0f);
        double area = 0.0;

        cv::Mat debug = frame.clone();

        // 在原始图像上找绿色目标，同时生成可视化调试图。
        detectGreenTarget(frame, debug, found, center, area);

        // 无论是否找到目标，都发布 found 状态，方便下游做丢失处理。
        std_msgs::msg::Bool found_msg;
        found_msg.data = found;
        found_pub_->publish(found_msg);

        if (found) {
            // 仅在找到目标时发布中心点，避免下游误用无效坐标。
            geometry_msgs::msg::PointStamped target_msg;
            target_msg.header.stamp = now;
            target_msg.header.frame_id = "camera";
            target_msg.point.x = center.x;
            target_msg.point.y = center.y;
            target_msg.point.z = area;
            target_pub_->publish(target_msg);
        }

        frame_id_++;

        // 压缩图像，每 2 帧发布一次；降低调试图带宽和 JPEG 编码开销。
        if (frame_id_ % 2 == 0) {
            cv::Mat debug_small;
            // 发布前缩小到 640x360，Foxglove 观察足够用，也更省网络。
            cv::resize(debug, debug_small, cv::Size(640, 360));

            publishCompressedImage(debug_small, "camera");
        }

        frame_count_++;

        // 每秒统计一次处理 FPS。
        double dt = (now - last_time_).seconds();
        if (dt >= 1.0) {
            std_msgs::msg::Float32 fps_msg;
            fps_msg.data = static_cast<float>(frame_count_ / dt);
            fps_pub_->publish(fps_msg);

            RCLCPP_INFO(this->get_logger(), "FPS: %.2f", fps_msg.data);

            frame_count_ = 0;
            last_time_ = now;
        }
    }

    void publishCompressedImage(const cv::Mat &image, const std::string &frame_id)
    {
        std::vector<uchar> encoded;
        // JPEG 质量 60：进行压缩，
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 60};

        bool ok = cv::imencode(".jpg", image, encoded, params);
        if (!ok) {
            RCLCPP_WARN(this->get_logger(), "JPEG encode failed");
            return;
        }

        sensor_msgs::msg::CompressedImage msg;
        msg.header.stamp = this->now();
        msg.header.frame_id = frame_id;
        msg.format = "jpeg";
        // encoded 是临时缓冲，move 到 ROS 消息里避免一次额外拷贝。
        msg.data = std::move(encoded);

        debug_pub_->publish(msg);
    }

    void detectGreenTarget(const cv::Mat &frame, cv::Mat &debug, bool &found, cv::Point2f &center, double &area)
    {
        cv::Mat hsv;
        // HSV 比 BGR 更适合按颜色阈值分割，H 表示色相，S/V 表示饱和度和亮度。
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

        cv::Mat mask;

        // 绿色阈值：H=35~85，过滤过暗或饱和度太低的像素。
        cv::inRange(hsv, cv::Scalar(35, 60, 60), cv::Scalar(85, 255, 255), mask);

        // 先腐蚀去小噪点，再膨胀补回目标主体。
        cv::erode(mask, mask, cv::Mat(), cv::Point(-1, -1), 1);
        cv::dilate(mask, mask, cv::Mat(), cv::Point(-1, -1), 2);

        std::vector<std::vector<cv::Point>> contours;
        // 只取外轮廓，减少内部孔洞对目标选择的影响。
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        found = false;
        area = 0.0;

        // 选择面积最大的绿色轮廓作为当前目标。
        for (const auto &c : contours) {
            double a = cv::contourArea(c);

            if (a > area) {
                // 用图像矩计算轮廓质心，作为目标中心。
                cv::Moments m = cv::moments(c);

                if (m.m00 > 1e-6) {
                    area = a;
                    center = cv::Point2f(static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00));
                    found = true;
                }
            }
        }

        // 面积太小通常是噪声，不认为是有效目标。
        if (!found || area < 50.0) {
            found = false;
        }

        // 白点表示画面中心，便于观察目标偏差。
        cv::circle(debug, cv::Point(frame.cols / 2, frame.rows / 2), 8, cv::Scalar(255, 255, 255), -1);

        if (found) {
            // 红点表示目标中心，蓝线表示从画面中心到目标中心的偏移。
            cv::circle(debug, center, 12, cv::Scalar(0, 0, 255), -1);

            cv::line(debug, cv::Point(frame.cols / 2, frame.rows / 2), center, cv::Scalar(255, 0, 0), 2);

            std::string info = "x=" + std::to_string(static_cast<int>(center.x)) +
                               " y=" + std::to_string(static_cast<int>(center.y)) +
                               " area=" + std::to_string(static_cast<int>(area));

            cv::putText(debug, info, cv::Point(30, 50), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        }
        else {
            // 未检测到目标时，在调试图上给出丢失提示。
            cv::putText(
                debug, "target lost", cv::Point(30, 50), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
        }
    }

private:
    V4L2Capture camera_;

    rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr debug_pub_;
    rclcpp::Publisher<geometry_msgs::msg::PointStamped>::SharedPtr target_pub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr found_pub_;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr fps_pub_;

    rclcpp::TimerBase::SharedPtr timer_;

    rclcpp::Time last_time_;
    int frame_count_ = 0;
    int frame_id_ = 0;
};

int main(int argc, char **argv)
{
    // 初始化 ROS 2 客户端库。
    rclcpp::init(argc, argv);

    try {
        // 创建节点并进入回调循环，直到进程退出或 ROS shutdown。
        rclcpp::spin(std::make_shared<VisionNode>());
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    // 释放 ROS 2 资源。
    rclcpp::shutdown();
    return 0;
}
