#include "vision_node/v4l2_capture.hpp"

#include <geometry_msgs/msg/point_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <vector>
using namespace std::chrono_literals;

class VisionNode : public rclcpp::Node
{
public:
    VisionNode() : Node("vision_node"), camera_("/dev/video0", 1920, 1080)
    {
        auto sensor_qos = rclcpp::SensorDataQoS().keep_last(1).best_effort();

        debug_pub_ =
            this->create_publisher<sensor_msgs::msg::CompressedImage>("/vision/debug_image/compressed", sensor_qos);

        target_pub_ = this->create_publisher<geometry_msgs::msg::PointStamped>("/vision/target_center", 1);

        found_pub_ = this->create_publisher<std_msgs::msg::Bool>("/vision/target_found", 1);

        fps_pub_ = this->create_publisher<std_msgs::msg::Float32>("/vision/fps", 1);

        if (!camera_.openCamera()) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open V4L2 camera");
            throw std::runtime_error("camera open failed");
        }

        last_time_ = this->now();

        timer_ = this->create_wall_timer(1ms, std::bind(&VisionNode::timerCallback, this));

        RCLCPP_INFO(this->get_logger(), "Vision node started");
    }

private:
    void timerCallback()
    {
        cv::Mat frame;

        if (!camera_.readFrame(frame)) {
            RCLCPP_WARN(this->get_logger(), "Failed to read frame");
            return;
        }

        auto now = this->now();

        bool found = false;
        cv::Point2f center(0.0f, 0.0f);
        double area = 0.0;

        cv::Mat debug = frame.clone();

        detectGreenTarget(frame, debug, found, center, area);

        std_msgs::msg::Bool found_msg;
        found_msg.data = found;
        found_pub_->publish(found_msg);

        if (found) {
            geometry_msgs::msg::PointStamped target_msg;
            target_msg.header.stamp = now;
            target_msg.header.frame_id = "camera";
            target_msg.point.x = center.x;
            target_msg.point.y = center.y;
            target_msg.point.z = area;
            target_pub_->publish(target_msg);
        }

        frame_id_++;

        // 压缩图像，每 2 帧发布一次。后面想更低延迟可以改成 %3 或 %4。
        if (frame_id_ % 2 == 0) {
            cv::Mat debug_small;
            cv::resize(debug, debug_small, cv::Size(640, 360));

            publishCompressedImage(debug_small, "camera");
        }

        frame_count_++;

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
        msg.data = std::move(encoded);

        debug_pub_->publish(msg);
    }

    void detectGreenTarget(const cv::Mat &frame, cv::Mat &debug, bool &found, cv::Point2f &center, double &area)
    {
        cv::Mat hsv;
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

        cv::Mat mask;

        cv::inRange(hsv, cv::Scalar(35, 60, 60), cv::Scalar(85, 255, 255), mask);

        cv::erode(mask, mask, cv::Mat(), cv::Point(-1, -1), 1);
        cv::dilate(mask, mask, cv::Mat(), cv::Point(-1, -1), 2);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        found = false;
        area = 0.0;

        for (const auto &c : contours) {
            double a = cv::contourArea(c);

            if (a > area) {
                cv::Moments m = cv::moments(c);

                if (m.m00 > 1e-6) {
                    area = a;
                    center = cv::Point2f(static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00));
                    found = true;
                }
            }
        }

        if (!found || area < 50.0) {
            found = false;
        }

        cv::circle(debug, cv::Point(frame.cols / 2, frame.rows / 2), 8, cv::Scalar(255, 255, 255), -1);

        if (found) {
            cv::circle(debug, center, 12, cv::Scalar(0, 0, 255), -1);

            cv::line(debug, cv::Point(frame.cols / 2, frame.rows / 2), center, cv::Scalar(255, 0, 0), 2);

            std::string info = "x=" + std::to_string(static_cast<int>(center.x)) +
                               " y=" + std::to_string(static_cast<int>(center.y)) +
                               " area=" + std::to_string(static_cast<int>(area));

            cv::putText(debug, info, cv::Point(30, 50), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        }
        else {
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
    rclcpp::init(argc, argv);

    try {
        rclcpp::spin(std::make_shared<VisionNode>());
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    rclcpp::shutdown();
    return 0;
}