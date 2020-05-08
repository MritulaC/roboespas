#include <cstdlib>
#include <iostream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <string>
#include "ros/ros.h"
#include "sensor_msgs/JointState.h"
#include "trajectory_msgs/JointTrajectoryPoint.h"
#include "trajectory_msgs/JointTrajectory.h"
#include <iiwa_command/MoveJAction.h>
#include <actionlib/server/simple_action_server.h>
#include <Eigen/Dense>
#include <math.h>
#include <cmath>

using namespace std;

class MoveJAction
{
    protected:
    ros::NodeHandle nh;
    //Iiwa command action server variables
    actionlib::SimpleActionServer<iiwa_command::MoveJAction> as;
    iiwa_command::MoveJFeedback as_feedback;
    iiwa_command::MoveJResult as_result;
    //Iiwa gazebo state subscriber
    ros::Subscriber iiwa_state_sub;
    sensor_msgs::JointState joint_state;
    //Iiwa gazebo command publisher
    ros::Publisher iiwa_command_pub;
    //Parameters
    double max_joint_position_inc;
    double control_step_size;
    std::string robot_mode;
    Eigen::VectorXd max_vel;
    Eigen::VectorXd max_pos;
    double error_joint_position_stop=0.0001;

    public:

    MoveJAction(std::string name) :
    as(nh, name, boost::bind(&MoveJAction::callback_MoveJ, this, _1), false) //Create the action server
    {
        as.start();
        ROS_INFO("Action server %s started", name.c_str());
        //Read parameters
        if(!nh.getParam("/iiwa_command/robot_mode", robot_mode))
        {
            ROS_ERROR("Failed to read '/iiwa_command/robot_mode' on param server");
        }
        if (!nh.getParam("/iiwa_command/control_step_size", control_step_size))
        {
            ROS_ERROR("Failed to read '/iiwa_command/control_step_size' on param server");
        }
        std::vector<double> max_pos_vec;
        if (!nh.getParam("/iiwa_limits/joint_position", max_pos_vec))
        {
            ROS_ERROR("Failed to read '/iiwa_limits/joint_position' on param server");
        }
        max_pos = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(max_pos_vec.data(), max_pos_vec.size());

        std::vector<double> max_vel_vec;
        if (!nh.getParam("/iiwa_limits/joint_velocity", max_vel_vec))
        {
            ROS_ERROR("Failed to read '/iiwa_limits/joint_velocity' on param server");
        }
        max_vel = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(max_vel_vec.data(), max_vel_vec.size());
        max_vel = max_vel*M_PI/180.0;
        //Initializate topics depending on robot_mode
        if (strcmp(robot_mode.c_str(), "gazebo")==0)
        {
            iiwa_command_pub = nh.advertise<trajectory_msgs::JointTrajectoryPoint>("/iiwa_gazebo/joint_command", 1000, false);
            iiwa_state_sub = nh.subscribe("/iiwa_gazebo/joint_state", 1000, &MoveJAction::callback_iiwa_gazebo_state, this);
        }
        else
        {
            ROS_ERROR("Not implemented yet");
        }        
    }
    void callback_iiwa_gazebo_state(const sensor_msgs::JointState& iiwa_gazebo_state_msg)
    {
        joint_state=iiwa_gazebo_state_msg;
    }
    void callback_MoveJ(const iiwa_command::MoveJGoalConstPtr &goal)
    {   
        ROS_INFO("MoveJ action server active");
        //Variables returned
        trajectory_msgs::JointTrajectory trajectory_commanded;
        std::vector<sensor_msgs::JointState> trajectory_joint_state;

        //Check position is not empty
        if (goal->joint_position.empty())
        {
            ROS_ERROR("Empty joint position");
            as.setSucceeded(as_result);
            return;
        }

        //TODO: Check position is inside the workspace
        //TODO: Limit velocity
        //TODO: Move more/less depending on the distance to the goal
        /*
        //Build trajectory to send
        //Read percentage velocity from parameter server
        double velocity=0.5; //from 0 to 1
        if (!nh.getParam("/iiwa_command/velocity", velocity))
        {
            ROS_ERROR("Failed to read '/iiwa_command/velocity' on param server, using 0.5");
        }
        //Calculate joint_velocity for each joint using the velocity percentage and the maximum velocity
        std::cout << velocity << std::endl;
        Eigen::VectorXd qdot = velocity*max_vel;*/


        //First save in an Eigen vector q_goal and q_curr
        std::vector<double> q_goal_vec=goal->joint_position;
        Eigen::VectorXd q_goal = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned> (q_goal_vec.data(), q_goal_vec.size());
        //Check maximum increment in radians for joint_position
        if (!nh.getParam("/iiwa_limits/joint_position_inc", max_joint_position_inc))
        {
            ROS_ERROR("Failed to read '/iiwa_limits/joint_position_inc' on param server");
        }
        max_joint_position_inc=max_joint_position_inc*0.9; //To ensure its inside the limits
        ros::Time tStartTraj = ros::Time::now();
        bool cont=true;
        while (cont)
        {
            sensor_msgs::JointState freezed_joint_state = joint_state;
            //Get current position;
            Eigen::VectorXd q_curr = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(freezed_joint_state.position.data(), freezed_joint_state.position.size());
            //Calculate the difference in radians        
            Eigen::VectorXd q_diff = q_goal-q_curr;
            //Calculate the sign (direction) of the movement to be done
            Eigen::VectorXd sign_diff = q_diff.array().sign();
            //Check which of the joints are not yet near their goal position
            Eigen::VectorXd far = (q_diff.array().abs()>=max_joint_position_inc).cast<double>();
            Eigen::VectorXd near = (q_diff.array().abs()<max_joint_position_inc).cast<double>();
            //Compose the instantaneous increment commanded
            Eigen::VectorXd q_inc = max_joint_position_inc*far.cwiseProduct(sign_diff) + q_diff.cwiseProduct(near);
            //Get the next joint position
            Eigen::VectorXd q_next = q_curr + q_inc;
            //Prepare variables to command
            trajectory_msgs::JointTrajectoryPoint point_command;
            for (int i=0; i<q_next.size(); i++)
            {
                point_command.positions.push_back(q_next[i]);
            }
            ros::Duration time_from_start = ros::Time::now()-tStartTraj;
            point_command.time_from_start = time_from_start;

            //Save point_commanded and joint_state into vectors
            trajectory_commanded.points.push_back(point_command);
            trajectory_joint_state.push_back(freezed_joint_state);
            as_feedback.joint_state = freezed_joint_state;
            as_feedback.point_commanded = point_command;
            as_feedback.time_from_start=time_from_start.toSec();
            as.publishFeedback(as_feedback);
            //Command it
            iiwa_command_pub.publish(point_command);
            cont = q_diff.array().abs().maxCoeff() > error_joint_position_stop;
            ros::Duration(control_step_size).sleep();
        }
        as_result.trajectory_joint_state=trajectory_joint_state;
        as_result.trajectory_commanded=trajectory_commanded;
        as.setSucceeded(as_result);
        ROS_INFO("MoveJ action server result sent");
    }
};

