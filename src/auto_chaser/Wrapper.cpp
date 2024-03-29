#include "auto_chaser/Wrapper.h"

Wrapper::Wrapper(){};

void Wrapper::init(ros::NodeHandle nh) {
    
    // the initialization for its own member variables 
    string mav_name;
    nh.param<string>("mav_name",mav_name,"firefly");
    nh.param("run_mode",run_mode,0);

    // this topic is used for the actual control 
    string control_topic = "/" + mav_name + "/" + mav_msgs::default_topics::COMMAND_TRAJECTORY;

    pub_control_mav = nh.advertise<trajectory_msgs::MultiDOFJointTrajectory>(
           control_topic.c_str(), 10);
        
    // only for visualization 
    pub_control_mav_vis = nh.advertise<geometry_msgs::PoseStamped>("mav_pose_desired",1);
    // sub classes initialization 
    objects_handler.init(nh);  
    chaser.init(nh);      


};


void Wrapper::session(double t){
    // (1) the information of objects handler 
    objects_handler.publish();
    objects_handler.tf_update();    
    // (2) chaser
    chaser.session(t);
    // (3) wrapper 
    if (chaser.is_complete_chasing_path or run_mode == 1){
        pub_control_pose(t);
        pub_control_traj(t);
    }
}

// chasing session called 
bool Wrapper::trigger_chasing(TimeSeries chasing_knots){
    
    
    double t_planning_start = chasing_knots(0);

    vector<Point>  target_pred_seq = objects_handler.get_prediction_seq();
    GridField * edf_grid_ptr = objects_handler.get_edf_grid_ptr();
    

    Point chaser_init_point;
    Twist chaser_init_vel; 
    Twist chaser_init_acc;

    if(not chaser.is_complete_chasing_path){    
        chaser_init_point = objects_handler.get_chaser_pose().pose.position;    
        chaser_init_vel = objects_handler.get_chaser_velocity();
        chaser_init_acc = objects_handler.get_chaser_acceleration();   
    }
    else{
        chaser_init_point = chaser.eval_point(t_planning_start);
        chaser_init_vel = chaser.eval_velocity(t_planning_start);
        chaser_init_acc = chaser.eval_acceleration(t_planning_start);
    }

    // chasing policy update 
    bool is_success = chaser.chase_update(edf_grid_ptr,target_pred_seq,chaser_init_point,chaser_init_vel,chaser_init_acc,chasing_knots);   
    if(is_success) 
        objects_handler.is_path_solved = true; // at least once solved, 

    return is_success;
}

// informative mode 
bool Wrapper::trigger_chasing(vector<Point> target_pred_seq,TimeSeries chasing_knots){
  


    double t_planning_start = chasing_knots(0);

    GridField * edf_grid_ptr = objects_handler.get_edf_grid_ptr();

    Point chaser_init_point;
    Twist chaser_init_vel; 
    Twist chaser_init_acc;

    if(not chaser.is_complete_chasing_path){    
        chaser_init_point = objects_handler.get_chaser_pose().pose.position;    
        chaser_init_vel = objects_handler.get_chaser_velocity();
        chaser_init_acc = objects_handler.get_chaser_acceleration();   
    }
    else{
        chaser_init_point = chaser.eval_point(t_planning_start);
        chaser_init_vel = chaser.eval_velocity(t_planning_start);
        chaser_init_acc = chaser.eval_acceleration(t_planning_start);
    }

    // chasing policy update 
    bool is_success = chaser.chase_update(edf_grid_ptr,target_pred_seq,chaser_init_point,chaser_init_vel,chaser_init_acc,chasing_knots);   

    if(is_success) 
        objects_handler.is_path_solved = true; // at least once solved, 

    // chasing policy update 
    return is_success;
}

/**
 * @brief evalate the latest control pose from chaser 
 * 
 * @param t_eval the evaluation time 
 * @return geometry_msgs::PoseStamped the control pose for MAV 
 */
geometry_msgs::PoseStamped Wrapper::get_control_pose(double t_eval){

    PoseStamped target_pose = objects_handler.get_target_pose(); // the current target pose of the latest
    PoseStamped chaser_pose = objects_handler.get_chaser_pose(); // the current chaser pose of the latest
    
    // decide yawing direction so that the local x-axis heads to the target 
    // For this, if the target has not been uploaded yet, then the MAV heads to the last observed target position 
    float yaw = atan2(-chaser_pose.pose.position.y+target_pose.pose.position.y,
                        -chaser_pose.pose.position.x+target_pose.pose.position.x);
    tf::Quaternion q;
    if(not chaser.is_complete_chasing_path)
        q.setRPY(0,0,0); // should hovering first 
    else
        q.setRPY(0,0,yaw);
    q.normalize(); // to avoid the numerical error 
    PoseStamped chaser_pose_desired;  
    Point chaser_point_desired = chaser.get_control_point(t_eval); 
    chaser_pose_desired.header.frame_id = objects_handler.get_world_frame_id();
    chaser_pose_desired.pose.position = chaser_point_desired;
    chaser_pose_desired.pose.orientation.x = q.x();
    chaser_pose_desired.pose.orientation.y = q.y();
    chaser_pose_desired.pose.orientation.z = q.z();
    chaser_pose_desired.pose.orientation.w = q.w();
    return chaser_pose_desired;
}
/**
 * @brief publish the control visualization (geometry_msgs) 
 * 
 * @param t_eval evaluation time 
 */
void Wrapper::pub_control_pose(double t_eval){
    pub_control_mav_vis.publish(get_control_pose(t_eval));
}
/**
 * @brief publish the control visualization (trajectory_msgs) 
 * 
 * @param t_eval 
 */
void Wrapper::pub_control_traj(double t_eval){

    float chaser_yaw_desired;
    PoseStamped chaser_pose_desired;

    // retrieve yaw first  
    chaser_pose_desired = get_control_pose(t_eval);
    
    // convert the pose information into trajectory_msgs (only conversion)
    
    // get the position 
    Vector3d chaser_point_desired;
    chaser_point_desired(0) = chaser_pose_desired.pose.position.x;
    chaser_point_desired(1) = chaser_pose_desired.pose.position.y;
    chaser_point_desired(2) = chaser_pose_desired.pose.position.z;
    // get the orientation 
    tf::Quaternion q;
    q.setX(chaser_pose_desired.pose.orientation.x);
    q.setY(chaser_pose_desired.pose.orientation.y);
    q.setZ(chaser_pose_desired.pose.orientation.z);
    q.setW(chaser_pose_desired.pose.orientation.w);
    tf::Matrix3x3 q_mat(q);
    double roll,pitch,yaw;
    q_mat.getRPY(roll,pitch,yaw);
    // finishing the trajectory topic 
    trajectory_msgs::MultiDOFJointTrajectory chaser_traj_desired;
    mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(chaser_point_desired,yaw, &chaser_traj_desired);
    pub_control_mav.publish(chaser_traj_desired);
}



