#pragma once

#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include "manager/node_manager.hpp"

namespace robosense
{
namespace lidar
{

/**
 * @brief Managed (lifecycle) wrapper around the rslidar_sdk NodeManager
 *        (ROS 2 only).
 *
 * Lets the LiDAR be started/stopped at runtime as a managed node instead of the
 * stock rslidar_sdk_node, which reads its YAML and blocks until SIGINT.
 *
 * State mapping:
 *   - on_configure : load + validate the SDK config YAML; no hardware touched.
 *   - on_activate  : build a NodeManager, init() (bind sockets, spawn decode
 *                    threads) and start(): point cloud / IMU start flowing.
 *   - on_deactivate: destroy the NodeManager; its destructor runs stop() once
 *                    (stops the driver, joins the decode threads). LiDAR idle.
 *   - on_cleanup /
 *     on_shutdown /
 *     on_error     : drop the manager + cached config (idempotent).
 *
 * A fresh NodeManager is built on every activation rather than gating one
 * manager's start()/stop(): the SDK's SourceDriver::stop() joins its decode
 * threads and is single-shot (the NodeManager destructor already calls it), so
 * it cannot be re-started. Rebuilding per activation is the only way to get
 * clean, repeatable activate/deactivate cycles while stop() runs exactly once
 * per manager.
 *
 * @note Inherited from the SDK: SourceDriver::init() calls exit(-1) on a
 *       driver-init failure (bad lidar_type, socket bind error, ...), which
 *       terminates the process rather than returning FAILURE from on_activate.
 *       YAML parse errors are still caught and surfaced as a configure FAILURE.
 */
class RsLidarLifecycleNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  using CallbackReturn =
      rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

  /**
   * @brief Construct the node (named "rslidar_lidar_publisher") and declare the
   *        `config_path` parameter.
   * @param options Node options forwarded to the base LifecycleNode.
   */
  explicit RsLidarLifecycleNode(
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  RsLidarLifecycleNode(const RsLidarLifecycleNode&) = delete;
  RsLidarLifecycleNode& operator=(const RsLidarLifecycleNode&) = delete;
  ~RsLidarLifecycleNode() override;

  /**
   * @brief Load the SDK config YAML; no hardware is touched.
   *
   * Reads the `config_path` parameter, falling back to the package's bundled
   * config/config.yaml when it is empty.
   *
   * @param state Previous lifecycle state (unused).
   * @return SUCCESS once the YAML is loaded; FAILURE if the file is missing or
   *         malformed.
   */
  CallbackReturn on_configure(const rclcpp_lifecycle::State& state) override;

  /**
   * @brief Build, init and start a fresh NodeManager so the LiDAR publishes.
   * @param state Previous lifecycle state (unused).
   * @return SUCCESS once the receiver is running; FAILURE if configure has not
   *         run or the manager throws on init/start.
   */
  CallbackReturn on_activate(const rclcpp_lifecycle::State& state) override;

  /**
   * @brief Destroy the NodeManager, halting all publishing.
   *
   * The manager's destructor stops the driver and joins the decode threads.
   *
   * @param state Previous lifecycle state (unused).
   * @return SUCCESS.
   */
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State& state) override;

  /**
   * @brief Release the manager and cached config, returning to unconfigured.
   * @param state Previous lifecycle state (unused).
   * @return SUCCESS.
   */
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State& state) override;

  /**
   * @brief Tear down on the way to the finalized state (same as on_cleanup).
   * @param state Previous lifecycle state (unused).
   * @return SUCCESS.
   */
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State& state) override;

  /**
   * @brief Best-effort teardown after a failed transition.
   * @param state Previous lifecycle state (unused).
   * @return SUCCESS.
   */
  CallbackReturn on_error(const rclcpp_lifecycle::State& state) override;

private:
  /**
   * @brief Drop the NodeManager (idempotent; safe when already null).
   *
   * reset() runs NodeManager::stop() exactly once via its destructor.
   */
  void teardown();

  YAML::Node config_;                    ///< Parsed SDK config, set in on_configure.
  bool config_loaded_{false};            ///< True once a valid config is loaded.
  std::shared_ptr<NodeManager> manager_; ///< Active SDK manager; null when idle.
};

}  // namespace lidar
}  // namespace robosense
