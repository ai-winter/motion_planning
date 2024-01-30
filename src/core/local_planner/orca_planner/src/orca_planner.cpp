#include <pluginlib/class_list_macros.h>
#include "orca_planner.h"

PLUGINLIB_EXPORT_CLASS(orca_planner::OrcaPlanner, nav_core::BaseLocalPlanner)

namespace orca_planner
{

OrcaPlanner::OrcaPlanner()
  : initialized_(false), costmap_ros_(nullptr), tf_(nullptr), odom_flag_(false), goal_reached_(false)
{
}

OrcaPlanner::OrcaPlanner(std::string name, tf2_ros::Buffer* tf, costmap_2d::Costmap2DROS* costmap_ros) : OrcaPlanner()
{
  initialize(name, tf, costmap_ros);
}

OrcaPlanner::~OrcaPlanner()
{
}

// Take note that tf::TransformListener* has been changed to tf2_ros::Buffer* in ROS Noetic
void OrcaPlanner::initialize(std::string name, tf2_ros::Buffer* tf, costmap_2d::Costmap2DROS* costmap_ros)
{
  if (!initialized_)
  {
    initialized_ = true;
    tf_ = tf;
    costmap_ros_ = costmap_ros;
    max_v_ = 0.5, min_v_ = 0.0, max_v_inc_ = 0.5;
    max_w_ = 1.57, min_w_ = 0.0, max_w_inc_ = 1.57;
    ros::NodeHandle nh = ros::NodeHandle("~/" + name);

    nh.param("agent_number", agent_number_, -1);
    nh.param("agent_id", agent_id_, -1);

    other_odoms_.resize(agent_number_);
    for (int i = 0; i < agent_number_; ++i)
    {
      ros::Subscriber odom_sub = nh.subscribe<nav_msgs::Odometry>(
          "/robot" + std::to_string(i + 1) + "/odom", 1, boost::bind(&OrcaPlanner::odometryCallback, this, _1, i + 1));
      odom_subs_.push_back(odom_sub);
      ROS_INFO("agent %d, subscribe to agent %d.", agent_id_, i + 1);
    }

    int spin_freq = 10;
    int spin_cnt = 5 * agent_number_;
    ros::Rate rate(spin_freq);
    ROS_WARN("[ORCA] Waiting for odoms...");
    while (spin_cnt-- > 0)
    {
      ros::spinOnce();
      rate.sleep();
    }

    double controller_freqency;
    nh.param("/move_base/controller_frequency", controller_freqency, 10.0);
    d_t_ = 1 / controller_freqency;

    sim_ = new RVO::RVOSimulator();
    initState();
    ROS_INFO("ORCA planner initialized!");
  }
  else
  {
    ROS_WARN("ORCA planner has already been initialized.");
  }
}

bool OrcaPlanner::setPlan(const std::vector<geometry_msgs::PoseStamped>& orig_global_plan)
{
  if (!initialized_)
  {
    ROS_ERROR("This planner has not been initialized");
    return false;
  }

  ROS_INFO("Got new plan");

  if (goal_.x() != orig_global_plan.back().pose.position.x || goal_.y() != orig_global_plan.back().pose.position.y)
  {
    goal_ = RVO::Vector2(orig_global_plan.back().pose.position.x, orig_global_plan.back().pose.position.y);
    goal_reached_ = false;
  }

  return true;
}

bool OrcaPlanner::computeVelocityCommands(geometry_msgs::Twist& cmd_vel)
{
  if (!initialized_)
  {
    ROS_ERROR("This planner has not been initialized");
    return false;
  }

  updateState();
  RVO::Vector2 new_speed = sim_->getAgentNewSpeed(agent_id_ - 1);

  // transform from linear.x and linear.y to linear.x and angular.z
  nav_msgs::Odometry agent_odom = other_odoms_[agent_id_ - 1];
  double theta = tf2::getYaw(agent_odom.pose.pose.orientation);

  double v_d = RVO::abs(new_speed);
  double theta_d = std::atan2(new_speed.y(), new_speed.x());
  double e_theta = regularizeAngle(theta_d - theta);
  double w_d = e_theta / d_t_;
  // ROS_WARN("new_speed: (%.2f, %.2f), theta_d: %.2f, theta: %.2f, e_theta: %.2f", new_speed.x(), new_speed.y(),
  // theta_d, theta, e_theta);

  cmd_vel.linear.x = linearRegularization(agent_odom, v_d);
  cmd_vel.angular.z = angularRegularization(agent_odom, w_d);

  return true;
}

bool OrcaPlanner::isGoalReached()
{
  if (!initialized_)
  {
    ROS_ERROR("ORCA planner has not been initialized");
    return false;
  }

  if (goal_reached_)
  {
    ROS_INFO("GOAL Reached!");
    return true;
  }
  return false;
}

void OrcaPlanner::initState()
{
  if (!odom_flag_)
  {
    ROS_ERROR("Odom not received!");
    return;
  }

  sim_->setTimeStep(d_t_);

  // neighborDist, maxNeighbors, timeHorizon, timeHorizonObst, radius, maxSpeed
  sim_->setAgentDefaults(3.0f, 10, 5.0f, 3.0f, 0.2f, max_v_);  // TODO

  for (int i = 0; i < agent_number_; ++i)
  {
    sim_->addAgent(RVO::Vector2(other_odoms_[i].pose.pose.position.x, other_odoms_[i].pose.pose.position.y));
    sim_->setAgentVelocity(i, RVO::Vector2(other_odoms_[i].twist.twist.linear.x, other_odoms_[i].twist.twist.linear.y));
  }
}

void OrcaPlanner::odometryCallback(const nav_msgs::OdometryConstPtr& msg, int agent_id)
{
  if (!odom_flag_)
    odom_flag_ = true;

  other_odoms_[agent_id - 1] = *msg;
}

void OrcaPlanner::updateState()
{
  for (int i = 0; i < agent_number_; ++i)
  {
    sim_->setAgentPosition(i, RVO::Vector2(other_odoms_[i].pose.pose.position.x, other_odoms_[i].pose.pose.position.y));
    sim_->setAgentVelocity(i, RVO::Vector2(other_odoms_[i].twist.twist.linear.x, other_odoms_[i].twist.twist.linear.y));
  }

  // TODO: 1m/s towards goal, set the custom prefered vel
  sim_->setAgentPrefVelocity(agent_id_ - 1, RVO::normalize(goal_ - sim_->getAgentPosition(agent_id_ - 1)) * max_v_);
}

}  // namespace orca_planner