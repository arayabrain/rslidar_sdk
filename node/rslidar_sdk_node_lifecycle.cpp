#include "rslidar_sdk_node_lifecycle.hpp"

#include <string>

namespace robosense
{
namespace lidar
{

RsLidarLifecycleNode::RsLidarLifecycleNode(const rclcpp::NodeOptions& options)
  : rclcpp_lifecycle::LifecycleNode("rslidar_lidar_publisher", options)
{
  // Path to the SDK config.yaml. Empty => fall back to the package default.
  this->declare_parameter<std::string>("config_path", "");
}

RsLidarLifecycleNode::~RsLidarLifecycleNode() { teardown(); }

RsLidarLifecycleNode::CallbackReturn RsLidarLifecycleNode::on_configure(
    const rclcpp_lifecycle::State& /*state*/)
{
  std::string config_path = this->get_parameter("config_path").as_string();
  if (config_path.empty())
  {
    config_path = std::string(PROJECT_PATH) + "/config/config.yaml";
  }

  try
  {
    config_ = YAML::LoadFile(config_path);
  }
  catch (...)
  {
    RCLCPP_ERROR(this->get_logger(),
                 "Failed to load config '%s' (check the path and YAML "
                 "indentation).",
                 config_path.c_str());
    return CallbackReturn::FAILURE;
  }

  config_loaded_ = true;
  RCLCPP_INFO(this->get_logger(),
              "Configured from '%s'. LiDAR idle (STANDBY) until activate.",
              config_path.c_str());
  return CallbackReturn::SUCCESS;
}

RsLidarLifecycleNode::CallbackReturn RsLidarLifecycleNode::on_activate(
    const rclcpp_lifecycle::State& /*state*/)
{
  if (!config_loaded_)
  {
    RCLCPP_ERROR(this->get_logger(), "Activate called before a valid configure.");
    return CallbackReturn::FAILURE;
  }

  // Fresh manager each activation (see the class doc for why).
  manager_ = std::make_shared<NodeManager>();
  try
  {
    manager_->init(config_);  // binds sockets, spawns decode threads
    manager_->start();        // receiver on; point cloud / IMU start flowing
  }
  catch (const std::exception& exc)
  {
    RCLCPP_ERROR(this->get_logger(), "NodeManager init/start failed: %s", exc.what());
    manager_.reset();
    return CallbackReturn::FAILURE;
  }

  RCLCPP_INFO(this->get_logger(),
              "Activated. LiDAR SCANNING; publishing point cloud / IMU.");
  return CallbackReturn::SUCCESS;
}

RsLidarLifecycleNode::CallbackReturn RsLidarLifecycleNode::on_deactivate(
    const rclcpp_lifecycle::State& /*state*/)
{
  // Destroying the manager runs NodeManager::stop() once (driver stop + join).
  manager_.reset();
  RCLCPP_INFO(this->get_logger(),
              "Deactivated. LiDAR receiver stopped (STANDBY).");
  return CallbackReturn::SUCCESS;
}

RsLidarLifecycleNode::CallbackReturn RsLidarLifecycleNode::on_cleanup(
    const rclcpp_lifecycle::State& /*state*/)
{
  teardown();
  config_loaded_ = false;
  return CallbackReturn::SUCCESS;
}

RsLidarLifecycleNode::CallbackReturn RsLidarLifecycleNode::on_shutdown(
    const rclcpp_lifecycle::State& /*state*/)
{
  teardown();
  config_loaded_ = false;
  return CallbackReturn::SUCCESS;
}

RsLidarLifecycleNode::CallbackReturn RsLidarLifecycleNode::on_error(
    const rclcpp_lifecycle::State& /*state*/)
{
  teardown();
  config_loaded_ = false;
  return CallbackReturn::SUCCESS;
}

void RsLidarLifecycleNode::teardown()
{
  manager_.reset();
}

}  // namespace lidar
}  // namespace robosense

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<robosense::lidar::RsLidarLifecycleNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node->get_node_base_interface());
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
