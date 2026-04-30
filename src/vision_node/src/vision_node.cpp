#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/timer.hpp>
#include <rclcpp/utilities.hpp>
#include <sensor_msgs/msg/detail/image__struct.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/detail/header__struct.hpp>

using namespace std::chrono_literals;

class VisionNode : public rclcpp::Node
{
public:
    VisionNode() : Node("vision_node")
    {
        system("v4l2-ctl -d /dev/v4l-subdev0 --set-subdev-selection "
               "pad=0,target=crop,left=0,top=0,width=1920,height=1080");
        system("v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=NV12");

        _cap.open(0);

        if (!_cap.isOpened()) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open camera");
            throw std::runtime_error("camera open failed");
        }

        // 强制分辨率
        _cap.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
        _cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);

        // 关键：格式转换
        _cap.set(cv::CAP_PROP_CONVERT_RGB, 1);

        // 可选（有些平台更稳）
        _cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

        _publisher = this->create_publisher<sensor_msgs::msg::Image>("/camera/image_raw", 10);

        _timer = this->create_wall_timer(33ms, std::bind(&VisionNode::timerCallback, this));
    }

private:
    void timerCallback()
    {
        cv::Mat frame;
        _cap >> frame;

        if (frame.empty()) {
            RCLCPP_WARN(this->get_logger(), "Empty frame");
            return;
        }

        //转 ROS Image
        auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", frame).toImageMsg();

        _publisher->publish(*msg);
    }
    cv::VideoCapture _cap;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr _publisher;
    rclcpp::TimerBase::SharedPtr _timer;
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