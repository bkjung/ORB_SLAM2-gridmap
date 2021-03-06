/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/


#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>

#include<ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>


//////////////////////////////////////////////////////////////////////////////////////////////////////
#include "sensor_msgs/PointCloud2.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PoseArray.h"
#include "nav_msgs/OccupancyGrid.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>

#include<opencv2/core/core.hpp>

#include <opencv2/highgui/highgui_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <Converter.h>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include <Eigen/Dense>

#ifndef DISABLE_FLANN
#include <flann/flann.hpp>
typedef flann::Index<flann::L2<double> > FLANN;
typedef std::unique_ptr<FLANN> FLANN_;
typedef flann::Matrix<double> flannMatT;
typedef flann::Matrix<int> flannResultT;
typedef std::unique_ptr<flannMatT> flannMatT_;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////


#include"../../../include/System.h"


//////////////////////////////////////////////////////////////////////////////////////////////////////
// parameters

float scale_factor = 10;
float resize_factor = 1;
float cloud_max_x = 29;
float cloud_min_x = -25;
float cloud_max_z = 48;
float cloud_min_z = -12;
float free_thresh = 0.55;
float occupied_thresh = 0.5;
unsigned int use_local_counters = 1;
int visit_thresh = 5;
unsigned int use_gaussian_counters = 1;
bool use_boundary_detection = 0;
bool use_height_thresholding = 1;
double normal_thresh_deg = 75.0;
int canny_thresh = 350;




// float scale_factor = 3;
// float resize_factor = 5;
// float cloud_max_x = 10;
// float cloud_min_x = -10.0;
// float cloud_max_z = 16;
// float cloud_min_z = -5;
// float free_thresh = 0.55;
// float occupied_thresh = 0.50;
float thresh_diff = 0.01;
// int visit_thresh = 0;
float upper_left_x = -1.5;
float upper_left_y = -2.5;
const int resolution = 10;
// unsigned int use_local_counters = 0;

// unsigned int use_gaussian_counters = 0;
// bool use_boundary_detection = false;
// bool use_height_thresholding = false;
// int canny_thresh = 350;
bool show_camera_location = true;
unsigned int gaussian_kernel_size = 3;
int cam_radius = 2;
// no. of keyframes between successive goal messages that are published
unsigned int goal_gap = 20;
bool enable_goal_publishing = false;

#ifndef DISABLE_FLANN
// double normal_thresh_deg = 0;
bool use_plane_normals = false;
double normal_thresh_y_rad=0.0;
std::vector<double> normal_angle_y;
FLANN_ flann_index;
#endif


float grid_max_x, grid_min_x,grid_max_z, grid_min_z;
cv::Mat global_occupied_counter, global_visit_counter;
cv::Mat local_occupied_counter, local_visit_counter;
cv::Mat local_map_pt_mask;
cv::Mat grid_map, grid_map_int, grid_map_thresh, grid_map_thresh_resized;
cv::Mat grid_map_rgb;
cv::Mat gauss_kernel;
float norm_factor_x, norm_factor_z;
float norm_factor_x_us, norm_factor_z_us;
int h, w;
unsigned int n_kf_received;
bool loop_closure_being_processed = false;
ros::Publisher pub_grid_map;
nav_msgs::OccupancyGrid grid_map_msg;

Eigen::Matrix4d transform_mat;

float kf_pos_x, kf_pos_z;
int kf_pos_grid_x, kf_pos_grid_z;
geometry_msgs::Point kf_location;
geometry_msgs::Quaternion kf_orientation;
unsigned int kf_id = 0;
unsigned int init_pose_id = 0, goal_id = 0;


bool pub_all_pts = false;
int all_pts_pub_gap = 0;
int pub_count = 0;

//////////////////////////////////////////////////////////////////////////////////////////////////////

using namespace std;

//////////////////////////////////////////////////////////////////////////////////////////////////////
void updateGridMap(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose);
void resetGridMap(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose);
void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& pt_cloud);
void kfCallback(const geometry_msgs::PoseStamped::ConstPtr& camera_pose);
void saveMap(unsigned int id = 0);
void ptCallback(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose);
void loopClosingCallback(const geometry_msgs::PoseArray::ConstPtr& all_kf_and_pts);
void showGridMap(unsigned int id = 0);
void getMixMax(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose,
	geometry_msgs::Point& min_pt, geometry_msgs::Point& max_pt);
void processMapPt(const geometry_msgs::Point &curr_pt, cv::Mat &occupied,
	cv::Mat &visited, cv::Mat &pt_mask, int kf_pos_grid_x, int kf_pos_grid_z);
void processMapPts(const std::vector<geometry_msgs::Pose> &pts, unsigned int n_pts,
	unsigned int start_id, int kf_pos_grid_x, int kf_pos_grid_z);
void getGridMap();
//////////////////////////////////////////////////////////////////////////////////////////////////////


void publish(ORB_SLAM2::System &SLAM, ros::Publisher &pub_pts_and_pose,
	ros::Publisher &pub_all_kf_and_pts, int frame_id);

class ImageGrabber{
public:
	ImageGrabber(ORB_SLAM2::System &_SLAM, ros::Publisher &_pub_pts_and_pose,
		ros::Publisher &_pub_all_kf_and_pts) :
		SLAM(_SLAM), pub_pts_and_pose(_pub_pts_and_pose),
		pub_all_kf_and_pts(_pub_all_kf_and_pts), frame_id(0){}

//	void GrabImage(const sensor_msgs::ImageConstPtr& msg);
    void GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD);

	ORB_SLAM2::System &SLAM;
	ros::Publisher &pub_pts_and_pose;
	ros::Publisher &pub_all_kf_and_pts;
	int frame_id;
};

//class ImageGrabber
//{
//public:
//    ImageGrabber(ORB_SLAM2::System* pSLAM):mpSLAM(pSLAM){}
//
//    void GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD);
//
//    ORB_SLAM2::System* mpSLAM;
//};

int main(int argc, char **argv)
{
    ros::init(argc, argv, "RGBD");
    ros::start();

    if(argc != 3)
    {
        cerr << endl << "Usage: rosrun ORB_SLAM2 RGBD path_to_vocabulary path_to_settings" << endl;        
        ros::shutdown();
        return 1;
    }    

//////////////////////////////////////////////////////////////////////////////////////////////////////
    grid_max_x = cloud_max_x*scale_factor;
	grid_min_x = cloud_min_x*scale_factor;
	grid_max_z = cloud_max_z*scale_factor;
	grid_min_z = cloud_min_z*scale_factor;
	printf("grid_max: %f, %f\t grid_min: %f, %f\n", grid_max_x, grid_max_z, grid_min_x, grid_min_z);

    double grid_res_x = grid_max_x - grid_min_x, grid_res_z = grid_max_z - grid_min_z;


	h = grid_res_z;
	w = grid_res_x;
	printf("grid_size: (%d, %d)\n", h, w);
	n_kf_received = 0;

	global_occupied_counter.create(h, w, CV_32SC1);
	global_visit_counter.create(h, w, CV_32SC1);
	global_occupied_counter.setTo(cv::Scalar(0));
	global_visit_counter.setTo(cv::Scalar(0));

	grid_map_msg.data.resize(h*w);
	grid_map_msg.info.width = w;
	grid_map_msg.info.height = h;
	grid_map_msg.info.resolution = 1.0/scale_factor;

	grid_map_int = cv::Mat(h, w, CV_8SC1, (char*)(grid_map_msg.data.data()));

	grid_map.create(h, w, CV_32FC1);
	grid_map_thresh.create(h, w, CV_8UC1);
	grid_map_thresh_resized.create(h*resize_factor, w*resize_factor, CV_8UC1);
	grid_map_rgb.create(h*resize_factor, w*resize_factor, CV_8UC3);
	printf("output_size: (%d, %d)\n", grid_map_thresh_resized.rows, grid_map_thresh_resized.cols);

	local_occupied_counter.create(h, w, CV_32SC1);
	local_visit_counter.create(h, w, CV_32SC1);
	local_map_pt_mask.create(h, w, CV_8UC1);

	gauss_kernel = cv::getGaussianKernel(gaussian_kernel_size, -1);


	norm_factor_x_us = float(cloud_max_x - cloud_min_x - 1) / float(cloud_max_x - cloud_min_x);
	norm_factor_z_us = float(cloud_max_z - cloud_min_z - 1) / float(cloud_max_z - cloud_min_z);


	norm_factor_x = float(grid_res_x - 1) / float(grid_max_x - grid_min_x);
	norm_factor_z = float(grid_res_z - 1) / float(grid_max_z - grid_min_z);
	printf("norm_factor_x: %f\n", norm_factor_x);
	printf("norm_factor_z: %f\n", norm_factor_z);
//////////////////////////////////////////////////////////////////////////////////////////////////////


    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::RGBD,true);
	
    //ImageGrabber igb(&SLAM);

    ros::NodeHandle nh;

//////////////////////////////////////////////////////////////////////////////////////////////////////
    ros::Subscriber sub_pts_and_pose = nh.subscribe("pts_and_pose", 1000, ptCallback);
	ros::Subscriber sub_all_kf_and_pts = nh.subscribe("all_kf_and_pts", 1000, loopClosingCallback);
    
    ros::Publisher pub_pts_and_pose = nh.advertise<geometry_msgs::PoseArray>("pts_and_pose", 1000);
	ros::Publisher pub_all_kf_and_pts = nh.advertise<geometry_msgs::PoseArray>("all_kf_and_pts", 1000);

    pub_grid_map = nh.advertise<nav_msgs::OccupancyGrid>("grid_map", 1000);

    ImageGrabber igb(SLAM, pub_pts_and_pose, pub_all_kf_and_pts);
//////////////////////////////////////////////////////////////////////////////////////////////////////

    message_filters::Subscriber<sensor_msgs::Image> rgb_sub(nh, "/camera/rgb/image_raw", 1);
    message_filters::Subscriber<sensor_msgs::Image> depth_sub(nh, "camera/depth_registered/image_raw", 1);
    typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::Image> sync_pol;
    message_filters::Synchronizer<sync_pol> sync(sync_pol(10), rgb_sub,depth_sub);
    sync.registerCallback(boost::bind(&ImageGrabber::GrabRGBD,&igb,_1,_2));

    ros::spin();

	printf("DEBUG-1\n");
    saveMap();
    // Stop all threads
    SLAM.Shutdown();

    
	printf("DEBUG-2\n");
    // Save camera trajectory
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");
    printf("DEBUG-3\n");

    ros::shutdown();
    printf("DEBUG-4\n");
    cv::destroyAllWindows();
    printf("DEBUG-5\n");
	
    return 0;
}

//void ImageGrabber::GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD)
//{
//    // Copy the ros image message to cv::Mat.
//    cv_bridge::CvImageConstPtr cv_ptrRGB;
//    try
//    {
//        cv_ptrRGB = cv_bridge::toCvShare(msgRGB);
//    }
//    catch (cv_bridge::Exception& e)
//    {
//        ROS_ERROR("cv_bridge exception: %s", e.what());
//        return;
//    }
//
//    cv_bridge::CvImageConstPtr cv_ptrD;
//    try
//    {
//        cv_ptrD = cv_bridge::toCvShare(msgD);
//    }
//    catch (cv_bridge::Exception& e)
//    {
//        ROS_ERROR("cv_bridge exception: %s", e.what());
//        return;
//    }
//
//    mpSLAM->TrackRGBD(cv_ptrRGB->image,cv_ptrD->image,cv_ptrRGB->header.stamp.toSec());
//}


//////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////


void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& pt_cloud){
	ROS_INFO("I heard: [%s]{%d}", pt_cloud->header.frame_id.c_str(),
		pt_cloud->header.seq);
}
void kfCallback(const geometry_msgs::PoseStamped::ConstPtr& camera_pose){
	ROS_INFO("I heard: [%s]{%d}", camera_pose->header.frame_id.c_str(),
		camera_pose->header.seq);
}
void saveMap(unsigned int id) {
	try{
		printf("saving maps with id: %d (debug)\n", id);
		time_t rawtime;   // get time now
		struct tm * timeinfo;

		time(&rawtime);
		timeinfo = localtime (&rawtime);
		char buffer [80];
		strftime (buffer,80,"%y%m%d_%H%M%S",timeinfo);

		// mkdir((string("/home/bkjung/ORB_SLAM2-gridmap/results/")+string(buffer)).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		system(("mkdir -p /home/bkjung/ORB_SLAM2-gridmap/results/"+string(buffer)).c_str());
		printf("/home/bkjung/ORB_SLAM2-gridmap/results/%s\n", string(buffer).c_str());
		//mkdir("results", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (id > 0) {
			cv::imwrite(("/home/bkjung/ORB_SLAM2-gridmap/results/" + string(buffer) + "/grid_map_" + to_string(id) + ".jpg").c_str(), grid_map);
			cv::imwrite(("/home/bkjung/ORB_SLAM2-gridmap/results/" + string(buffer) + "/grid_map_thresh_" + to_string(id) + ".jpg").c_str(), grid_map_thresh);
			cv::imwrite(("/home/bkjung/ORB_SLAM2-gridmap/results/" + string(buffer) + "/grid_map_thresh_resized" + to_string(id) + ".jpg").c_str(), grid_map_thresh_resized);
		}
		else {
			cv::imwrite(("/home/bkjung/ORB_SLAM2-gridmap/results/" + string(buffer) + "/grid_map.jpg").c_str(), grid_map);
			cv::imwrite(("/home/bkjung/ORB_SLAM2-gridmap/results/" + string(buffer) + "/grid_map_thresh.jpg").c_str(), grid_map_thresh);
			cv::imwrite(("/home/bkjung/ORB_SLAM2-gridmap/results/" + string(buffer) + "/grid_map_thresh_resized.jpg").c_str(), grid_map_thresh_resized);
		}
		printf("saved maps!!!\n");
	}
	catch (...) {
		printf("error in saveMap\n");
	}

}
void ptCallback(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose){
	//ROS_INFO("Received points and pose: [%s]{%d}", pts_and_pose->header.frame_id.c_str(),
	//	pts_and_pose->header.seq);
	//if (pts_and_pose->header.seq==0) {
	//	cv::destroyAllWindows();
	//	saveMap();
	//	printf("Received exit message\n");
	//	ros::shutdown();
	//	exit(0);
	//}
//	if (!got_start_time) {
//#ifdef COMPILEDWITHC11
//		start_time = std::chrono::steady_clock::now();
//#else
//		start_time = std::chrono::monotonic_clock::now();
//#endif
//		got_start_time = true;
//	}

	if (loop_closure_being_processed){ return; }

	updateGridMap(pts_and_pose);

	grid_map_msg.info.map_load_time = ros::Time::now();
	pub_grid_map.publish(grid_map_msg);
}
void loopClosingCallback(const geometry_msgs::PoseArray::ConstPtr& all_kf_and_pts){
	//ROS_INFO("Received points and pose: [%s]{%d}", pts_and_pose->header.frame_id.c_str(),
	//	pts_and_pose->header.seq);
	//if (all_kf_and_pts->header.seq == 0) {
	//	cv::destroyAllWindows();
	//	saveMap();
	//	ros::shutdown();
	//	exit(0);
	//}
	loop_closure_being_processed = true;
	resetGridMap(all_kf_and_pts);
	loop_closure_being_processed = false;
}

void getMixMax(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose,
	geometry_msgs::Point& min_pt, geometry_msgs::Point& max_pt) {

	min_pt.x = min_pt.y = min_pt.z = std::numeric_limits<double>::infinity();
	max_pt.x = max_pt.y = max_pt.z = -std::numeric_limits<double>::infinity();
	for (unsigned int i = 0; i < pts_and_pose->poses.size(); ++i){
		const geometry_msgs::Point& curr_pt = pts_and_pose->poses[i].position;
		if (curr_pt.x < min_pt.x) { min_pt.x = curr_pt.x; }
		if (curr_pt.y < min_pt.y) { min_pt.y = curr_pt.y; }
		if (curr_pt.z < min_pt.z) { min_pt.z = curr_pt.z; }

		if (curr_pt.x > max_pt.x) { max_pt.x = curr_pt.x; }
		if (curr_pt.y > max_pt.y) { max_pt.y = curr_pt.y; }
		if (curr_pt.z > max_pt.z) { max_pt.z = curr_pt.z; }
	}
}
void processMapPt(const geometry_msgs::Point &curr_pt, cv::Mat &occupied, cv::Mat &visited, 
	cv::Mat &pt_mask, int kf_pos_grid_x, int kf_pos_grid_z, unsigned int pt_id) {
	float pt_pos_x = curr_pt.x*scale_factor;
	float pt_pos_z = curr_pt.z*scale_factor;

	int pt_pos_grid_x = int(floor((pt_pos_x - grid_min_x) * norm_factor_x));
	int pt_pos_grid_z = int(floor((pt_pos_z - grid_min_z) * norm_factor_z));

	if (pt_pos_grid_x < 0 || pt_pos_grid_x >= w)
		return;

	if (pt_pos_grid_z < 0 || pt_pos_grid_z >= h)
		return;
	bool is_ground_pt = false;
	bool is_in_horizontal_plane = false;
	if (use_height_thresholding){
		float pt_pos_y = curr_pt.y*scale_factor;
		Eigen::Vector4d transformed_point_location = transform_mat * Eigen::Vector4d(pt_pos_x, pt_pos_y, pt_pos_z, 1);
		double transformed_point_height = transformed_point_location[1] / transformed_point_location[3];
		is_ground_pt = transformed_point_height < 0;
	}
#ifndef DISABLE_FLANN
	if (use_plane_normals) {
		double normal_angle_y_rad = normal_angle_y[pt_id];
		is_in_horizontal_plane = normal_angle_y_rad < normal_thresh_y_rad;
	}
#endif
	if (is_ground_pt || is_in_horizontal_plane) {
		++visited.at<float>(pt_pos_grid_z, pt_pos_grid_x);
	}
	else {
		// Increment the occupency account of the grid cell where map point is located
		++occupied.at<float>(pt_pos_grid_z, pt_pos_grid_x);
		pt_mask.at<uchar>(pt_pos_grid_z, pt_pos_grid_x) = 255;
	}	

	//cout << "----------------------" << endl;
	//cout << okf_pos_grid_x << " " << okf_pos_grid_y << endl;

	// Get all grid cell that the line between keyframe and map point pass through
	int x0 = kf_pos_grid_x;
	int y0 = kf_pos_grid_z;
	int x1 = pt_pos_grid_x;
	int y1 = pt_pos_grid_z;
	bool steep = (abs(y1 - y0) > abs(x1 - x0));
	if (steep){
		swap(x0, y0);
		swap(x1, y1);
	}
	if (x0 > x1){
		swap(x0, x1);
		swap(y0, y1);
	}
	int dx = x1 - x0;
	int dy = abs(y1 - y0);
	double error = 0;
	double deltaerr = ((double)dy) / ((double)dx);
	int y = y0;
	int ystep = (y0 < y1) ? 1 : -1;
	for (int x = x0; x <= x1; ++x){
		if (steep) {
			++visited.at<float>(x, y);
		}
		else {
			++visited.at<float>(y, x);
		}
		error = error + deltaerr;
		if (error >= 0.5){
			y = y + ystep;
			error = error - 1.0;
		}
	}
}

void processMapPts(const std::vector<geometry_msgs::Pose> &pts, unsigned int n_pts,
	unsigned int start_id, int kf_pos_grid_x, int kf_pos_grid_z) {
	unsigned int end_id = start_id + n_pts;
#ifndef DISABLE_FLANN
	if (use_plane_normals) {
		cv::Mat cv_dataset(n_pts, 3, CV_64FC1);
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			cv_dataset.at<double>(pt_id - start_id, 0) = pts[pt_id].position.x;
			cv_dataset.at<double>(pt_id - start_id, 1) = pts[pt_id].position.y;
			cv_dataset.at<double>(pt_id - start_id, 2) = pts[pt_id].position.z;
		}
		//printf("building FLANN index...\n");		
		flann_index->buildIndex(flannMatT((double *)(cv_dataset.data), n_pts, 3));
		normal_angle_y.resize(n_pts);
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			double pt[3], dists[3];
			pt[0] = pts[pt_id].position.x;
			pt[1] = pts[pt_id].position.y;
			pt[2] = pts[pt_id].position.z;

			int results[3];
			flannMatT flann_query(pt, 1, 3);
			flannMatT flann_dists(dists, 1, 3);
			flannResultT flann_result(results, 3, 1);
			flann_index->knnSearch(flann_query, flann_result, flann_dists, 3, flann::SearchParams());
			Eigen::Matrix3d nearest_pts;
			//printf("Point %d: %f, %f, %f\n", pt_id - start_id, pt[0], pt[1], pt[2]);
			for (unsigned int i = 0; i < 3; ++i){
				nearest_pts(0, i) = cv_dataset.at<double>(results[i], 0);
				nearest_pts(1, i) = cv_dataset.at<double>(results[i], 1);
				nearest_pts(2, i) = cv_dataset.at<double>(results[i], 2);
				//printf("Nearest Point %d: %f, %f, %f\n", results[i], nearest_pts(0, i), nearest_pts(1, i), nearest_pts(2, i));
			}
			Eigen::Vector3d centroid = nearest_pts.rowwise().mean();
			//printf("centroid %f, %f, %f\n", centroid[0], centroid[1], centroid[2]);
			Eigen::Matrix3d centered_pts = nearest_pts.colwise() - centroid;
			Eigen::JacobiSVD<Eigen::Matrix3d> svd(centered_pts, Eigen::ComputeThinU | Eigen::ComputeThinV);
			int n_cols = svd.matrixU().cols();
			// left singular vector corresponding to the smallest singular value
			Eigen::Vector3d normal_direction = svd.matrixU().col(n_cols - 1);
			//printf("normal_direction %f, %f, %f\n", normal_direction[0], normal_direction[1], normal_direction[2]);
			// angle to y axis
			normal_angle_y[pt_id-start_id] = acos(normal_direction[1]);
			if (normal_angle_y[pt_id - start_id ]> (M_PI / 2.0)) {
				normal_angle_y[pt_id - start_id] = M_PI - normal_angle_y[pt_id - start_id];
			}
			//printf("normal angle: %f rad or %f deg\n", normal_angle_y[pt_id - start_id], normal_angle_y[pt_id - start_id]*180.0/M_PI);
			//printf("\n\n");
		}
	}
#endif
	if (use_local_counters) {
		local_map_pt_mask.setTo(0);
		local_occupied_counter.setTo(0);
		local_visit_counter.setTo(0);
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			processMapPt(pts[pt_id].position, local_occupied_counter, local_visit_counter,
				local_map_pt_mask, kf_pos_grid_x, kf_pos_grid_z, pt_id - start_id);
		}
		for (int row = 0; row < h; ++row){
			for (int col = 0; col < w; ++col){
				if (local_map_pt_mask.at<uchar>(row, col) == 0) {
					local_occupied_counter.at<float>(row, col) = 0;
				}
				else {
					local_occupied_counter.at<float>(row, col) = local_visit_counter.at<float>(row, col);
				}
			}
		}
		if (use_gaussian_counters) {
			cv::filter2D(local_occupied_counter, local_occupied_counter, CV_32F, gauss_kernel);
			cv::filter2D(local_visit_counter, local_visit_counter, CV_32F, gauss_kernel);
		}
		global_occupied_counter += local_occupied_counter;
		global_visit_counter += local_visit_counter;
		//cout << "local_occupied_counter: \n" << local_occupied_counter << "\n";
		//cout << "global_occupied_counter: \n" << global_occupied_counter << "\n";
		//cout << "local_visit_counter: \n" << local_visit_counter << "\n";
		//cout << "global_visit_counter: \n" << global_visit_counter << "\n";
	}
	else {
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			processMapPt(pts[pt_id].position, global_occupied_counter, global_visit_counter,
				local_map_pt_mask, kf_pos_grid_x, kf_pos_grid_z, pt_id - start_id);
		}
	}
}


void updateGridMap(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose){

	//geometry_msgs::Point min_pt, max_pt;
	//getMixMax(pts_and_pose, min_pt, max_pt);
	//printf("max_pt: %f, %f\t min_pt: %f, %f\n", max_pt.x*scale_factor, max_pt.z*scale_factor, 
	//	min_pt.x*scale_factor, min_pt.z*scale_factor);

	//double grid_res_x = max_pt.x - min_pt.x, grid_res_z = max_pt.z - min_pt.z;

	//printf("Received frame %u \n", pts_and_pose->header.seq);

	kf_location = pts_and_pose->poses[0].position;
	kf_orientation = pts_and_pose->poses[0].orientation;

	kf_pos_x = kf_location.x*scale_factor;
	kf_pos_z = kf_location.z*scale_factor;

	kf_pos_grid_x = int(floor((kf_pos_x - grid_min_x) * norm_factor_x));
	kf_pos_grid_z = int(floor((kf_pos_z - grid_min_z) * norm_factor_z));

	if (kf_pos_grid_x < 0 || kf_pos_grid_x >= w)
		return;

	if (kf_pos_grid_z < 0 || kf_pos_grid_z >= h)
		return;

	++n_kf_received;

	if (use_height_thresholding){
		Eigen::Vector4d kf_orientation_eig(kf_orientation.w, kf_orientation.x, kf_orientation.y, kf_orientation.z);
		kf_orientation_eig.array() /= kf_orientation_eig.norm();
		Eigen::Matrix3d keyframe_rotation = Eigen::Quaterniond(kf_orientation_eig).toRotationMatrix();
		Eigen::Vector3d keyframe_translation(kf_location.x*scale_factor, kf_location.y*scale_factor, kf_location.z*scale_factor);
		transform_mat.setIdentity();
		transform_mat.topLeftCorner<3, 3>() = keyframe_rotation.transpose();
		transform_mat.topRightCorner<3, 1>() = (-keyframe_rotation.transpose() * keyframe_translation);
	}
	unsigned int n_pts = pts_and_pose->poses.size() - 1;
	//printf("Processing key frame %u and %u points\n",n_kf_received, n_pts);
	processMapPts(pts_and_pose->poses, n_pts, 1, kf_pos_grid_x, kf_pos_grid_z);

	getGridMap();
	//showGridMap(pts_and_pose->header.seq);
	//cout << endl << "Grid map saved!" << endl;
}


void resetGridMap(const geometry_msgs::PoseArray::ConstPtr& all_kf_and_pts){
	global_visit_counter.setTo(0);
	global_occupied_counter.setTo(0);

	unsigned int n_kf = all_kf_and_pts->poses[0].position.x;
	if ((unsigned int) (all_kf_and_pts->poses[0].position.y) != n_kf ||
		(unsigned int) (all_kf_and_pts->poses[0].position.z) != n_kf) {
		printf("resetGridMap :: Unexpected formatting in the keyframe count element\n");
		return;
	}
	printf("Resetting grid map with %d key frames\n", n_kf);
#ifdef COMPILEDWITHC11
	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
#else
	std::chrono::monotonic_clock::time_point t1 = std::chrono::monotonic_clock::now();
#endif
	unsigned int id = 0;
	for (unsigned int kf_id = 0; kf_id < n_kf; ++kf_id){
		const geometry_msgs::Point &kf_location = all_kf_and_pts->poses[++id].position;
		//const geometry_msgs::Quaternion &kf_orientation = pts_and_pose->poses[0].orientation;
		unsigned int n_pts = all_kf_and_pts->poses[++id].position.x;
		if ((unsigned int)(all_kf_and_pts->poses[id].position.y) != n_pts ||
			(unsigned int)(all_kf_and_pts->poses[id].position.z) != n_pts) {
			printf("resetGridMap :: Unexpected formatting in the point count element for keyframe %d\n", kf_id);
			return;
		}
		float kf_pos_x = kf_location.x*scale_factor;
		float kf_pos_z = kf_location.z*scale_factor;

		int kf_pos_grid_x = int(floor((kf_pos_x - grid_min_x) * norm_factor_x));
		int kf_pos_grid_z = int(floor((kf_pos_z - grid_min_z) * norm_factor_z));

		if (kf_pos_grid_x < 0 || kf_pos_grid_x >= w)
			continue;

		if (kf_pos_grid_z < 0 || kf_pos_grid_z >= h)
			continue;

		if (id + n_pts >= all_kf_and_pts->poses.size()) {
			printf("resetGridMap :: Unexpected end of the input array while processing keyframe %u with %u points: only %u out of %u elements found\n",
				kf_id, n_pts, all_kf_and_pts->poses.size(), id + n_pts);
			return;
		}
		processMapPts(all_kf_and_pts->poses, n_pts, id + 1, kf_pos_grid_x, kf_pos_grid_z);
		id += n_pts;
	}	
	getGridMap();
#ifdef COMPILEDWITHC11
	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
#else
	std::chrono::monotonic_clock::time_point t2 = std::chrono::monotonic_clock::now();
#endif
	double ttrack = std::chrono::duration_cast<std::chrono::duration<double> >(t2 - t1).count();
	printf("Done. Time taken: %f secs\n", ttrack);
	pub_grid_map.publish(grid_map_msg);
	//showGridMap(all_kf_and_pts->header.seq);
}

void getGridMap() {
	for (int row = 0; row < h; ++row){
		for (int col = 0; col < w; ++col){
			int visits = global_visit_counter.at<int>(row, col);
			int occupieds = global_occupied_counter.at<int>(row, col);

			if (visits <= visit_thresh){
				grid_map.at<float>(row, col) = 0.5;
			}
			else {
				grid_map.at<float>(row, col) = 1.0 - float(occupieds) / float(visits);
			}
			if (grid_map.at<float>(row, col) >= free_thresh) {
				grid_map_thresh.at<uchar>(row, col) = 255;
			}
			else if (grid_map.at<float>(row, col) < free_thresh && grid_map.at<float>(row, col) >= occupied_thresh) {
				grid_map_thresh.at<uchar>(row, col) = 128;
			}
			else {
				grid_map_thresh.at<uchar>(row, col) = 0;
			}
			grid_map_int.at<char>(row, col) = (1 - grid_map.at<float>(row, col)) * 100;
		}
	}
	cv::resize(grid_map_thresh, grid_map_thresh_resized, grid_map_thresh_resized.size());
}
void showGridMap(unsigned int id) {
	cv::imshow("grid_map_msg", cv::Mat(h, w, CV_8SC1, (char*)(grid_map_msg.data.data())));
	cv::imshow("grid_map_thresh_resized", grid_map_thresh_resized);
	//cv::imshow("grid_map", grid_map);
	int key = cv::waitKey(1) % 256;
	if (key == 27) {
		cv::destroyAllWindows();
		ros::shutdown();
		exit(0);
	}
	else if (key == 'f') {
		free_thresh -= thresh_diff;
		if (free_thresh <= occupied_thresh){ free_thresh = occupied_thresh + thresh_diff; }

		printf("Setting free_thresh to: %f\n", free_thresh);
	}
	else if (key == 'F') {
		free_thresh += thresh_diff;
		if (free_thresh > 1){ free_thresh = 1; }
		printf("Setting free_thresh to: %f\n", free_thresh);
	}
	else if (key == 'o') {
		occupied_thresh -= thresh_diff;
		if (free_thresh < 0){ free_thresh = 0; }
		printf("Setting occupied_thresh to: %f\n", occupied_thresh);
	}
	else if (key == 'O') {
		occupied_thresh += thresh_diff;
		if (occupied_thresh >= free_thresh){ occupied_thresh = free_thresh - thresh_diff; }
		printf("Setting occupied_thresh to: %f\n", occupied_thresh);
	}
	else if (key == 's') {
		saveMap(id);
	}
}


void ImageGrabber::GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD){
    // Copy the ros image message to cv::Mat.
    cv_bridge::CvImageConstPtr cv_ptrRGB;
    try
    {
        cv_ptrRGB = cv_bridge::toCvShare(msgRGB);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    cv_bridge::CvImageConstPtr cv_ptrD;
    try
    {
        cv_ptrD = cv_bridge::toCvShare(msgD);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    SLAM.TrackRGBD(cv_ptrRGB->image,cv_ptrD->image,cv_ptrRGB->header.stamp.toSec());
    publish(SLAM, pub_pts_and_pose, pub_all_kf_and_pts, frame_id);
    ++frame_id;
}


void publish(ORB_SLAM2::System &SLAM, ros::Publisher &pub_pts_and_pose,
	ros::Publisher &pub_all_kf_and_pts, int frame_id) {
	if (all_pts_pub_gap > 0 && pub_count >= all_pts_pub_gap) {
		pub_all_pts = true;
		pub_count = 0;
	}
	if (pub_all_pts || SLAM.getLoopClosing()->loop_detected || SLAM.getTracker()->loop_detected) {
		pub_all_pts = SLAM.getTracker()->loop_detected = SLAM.getLoopClosing()->loop_detected = false;
		geometry_msgs::PoseArray kf_pt_array;
		vector<ORB_SLAM2::KeyFrame*> key_frames = SLAM.getMap()->GetAllKeyFrames();
		//! placeholder for number of keyframes
		kf_pt_array.poses.push_back(geometry_msgs::Pose());
		sort(key_frames.begin(), key_frames.end(), ORB_SLAM2::KeyFrame::lId);
		unsigned int n_kf = 0;
		unsigned int n_pts_id = 0;
		for (auto key_frame : key_frames) {
			// pKF->SetPose(pKF->GetPose()*Two);

			if (!key_frame || key_frame->isBad()) {
				continue;
			}

			cv::Mat R = key_frame->GetRotation().t();
			vector<float> q = ORB_SLAM2::Converter::toQuaternion(R);
			cv::Mat twc = key_frame->GetCameraCenter();
			geometry_msgs::Pose kf_pose;

			kf_pose.position.x = twc.at<float>(0);
			kf_pose.position.y = twc.at<float>(1);
			kf_pose.position.z = twc.at<float>(2);
			kf_pose.orientation.x = q[0];
			kf_pose.orientation.y = q[1];
			kf_pose.orientation.z = q[2];
			kf_pose.orientation.w = q[3];
			kf_pt_array.poses.push_back(kf_pose);

			n_pts_id = kf_pt_array.poses.size();
			//! placeholder for number of points
			kf_pt_array.poses.push_back(geometry_msgs::Pose());
			std::set<ORB_SLAM2::MapPoint*> map_points = key_frame->GetMapPoints();
			unsigned int n_pts = 0;
			for (auto map_pt : map_points) {
				if (!map_pt || map_pt->isBad()) {
					//printf("Point %d is bad\n", pt_id);
					continue;
				}
				cv::Mat pt_pose = map_pt->GetWorldPos();
				if (pt_pose.empty()) {
					//printf("World position for point %d is empty\n", pt_id);
					continue;
				}
				geometry_msgs::Pose curr_pt;
				//printf("wp size: %d, %d\n", wp.rows, wp.cols);
				//pcl_cloud->push_back(pcl::PointXYZ(wp.at<float>(0), wp.at<float>(1), wp.at<float>(2)));
				curr_pt.position.x = pt_pose.at<float>(0);
				curr_pt.position.y = pt_pose.at<float>(1);
				curr_pt.position.z = pt_pose.at<float>(2);
				kf_pt_array.poses.push_back(curr_pt);
				++n_pts;
			}
			kf_pt_array.poses[n_pts_id].position.x = (double)n_pts;
			kf_pt_array.poses[n_pts_id].position.y = (double)n_pts;
			kf_pt_array.poses[n_pts_id].position.z = (double)n_pts;
			++n_kf;
		}
		kf_pt_array.poses[0].position.x = (double)n_kf;
		kf_pt_array.poses[0].position.y = (double)n_kf;
		kf_pt_array.poses[0].position.z = (double)n_kf;
		kf_pt_array.header.frame_id = "1";
		kf_pt_array.header.seq = frame_id + 1;
		printf("Publishing data for %u keyfranmes\n", n_kf);
		pub_all_kf_and_pts.publish(kf_pt_array);
	}
	else if (SLAM.getTracker()->mCurrentFrame.is_keyframe) {
		++pub_count;
		SLAM.getTracker()->mCurrentFrame.is_keyframe = false;
		ORB_SLAM2::KeyFrame* pKF = SLAM.getTracker()->mCurrentFrame.mpReferenceKF;

		cv::Mat Trw = cv::Mat::eye(4, 4, CV_32F);

		// If the reference keyframe was culled, traverse the spanning tree to get a suitable keyframe.
		//while (pKF->isBad())
		//{
		//	Trw = Trw*pKF->mTcp;
		//	pKF = pKF->GetParent();
		//}

		vector<ORB_SLAM2::KeyFrame*> vpKFs = SLAM.getMap()->GetAllKeyFrames();
		sort(vpKFs.begin(), vpKFs.end(), ORB_SLAM2::KeyFrame::lId);

		// Transform all keyframes so that the first keyframe is at the origin.
		// After a loop closure the first keyframe might not be at the origin.
		cv::Mat Two = vpKFs[0]->GetPoseInverse();

		Trw = Trw*pKF->GetPose()*Two;
		cv::Mat lit = SLAM.getTracker()->mlRelativeFramePoses.back();
		cv::Mat Tcw = lit*Trw;
		cv::Mat Rwc = Tcw.rowRange(0, 3).colRange(0, 3).t();
		cv::Mat twc = -Rwc*Tcw.rowRange(0, 3).col(3);

		vector<float> q = ORB_SLAM2::Converter::toQuaternion(Rwc);
		//geometry_msgs::Pose camera_pose;
		//std::vector<ORB_SLAM2::MapPoint*> map_points = SLAM.getMap()->GetAllMapPoints();
		std::vector<ORB_SLAM2::MapPoint*> map_points = SLAM.GetTrackedMapPoints();
		int n_map_pts = map_points.size();

		//printf("n_map_pts: %d\n", n_map_pts);

		//pcl::PointCloud<pcl::PointXYZ>::Ptr pcl_cloud(new pcl::PointCloud<pcl::PointXYZ>);

		geometry_msgs::PoseArray pt_array;
		//pt_array.poses.resize(n_map_pts + 1);

		geometry_msgs::Pose camera_pose;

		camera_pose.position.x = twc.at<float>(0);
		camera_pose.position.y = twc.at<float>(1);
		camera_pose.position.z = twc.at<float>(2);

		camera_pose.orientation.x = q[0];
		camera_pose.orientation.y = q[1];
		camera_pose.orientation.z = q[2];
		camera_pose.orientation.w = q[3];

		pt_array.poses.push_back(camera_pose);

		//printf("Done getting camera pose\n");

		for (int pt_id = 1; pt_id <= n_map_pts; ++pt_id){

			if (!map_points[pt_id - 1] || map_points[pt_id - 1]->isBad()) {
				//printf("Point %d is bad\n", pt_id);
				continue;
			}
			cv::Mat wp = map_points[pt_id - 1]->GetWorldPos();

			if (wp.empty()) {
				//printf("World position for point %d is empty\n", pt_id);
				continue;
			}
			geometry_msgs::Pose curr_pt;
			//printf("wp size: %d, %d\n", wp.rows, wp.cols);
			//pcl_cloud->push_back(pcl::PointXYZ(wp.at<float>(0), wp.at<float>(1), wp.at<float>(2)));
			curr_pt.position.x = wp.at<float>(0);
			curr_pt.position.y = wp.at<float>(1);
			curr_pt.position.z = wp.at<float>(2);
			pt_array.poses.push_back(curr_pt);
			//printf("Done getting map point %d\n", pt_id);
		}
		//sensor_msgs::PointCloud2 ros_cloud;
		//pcl::toROSMsg(*pcl_cloud, ros_cloud);
		//ros_cloud.header.frame_id = "1";
		//ros_cloud.header.seq = ni;

		//printf("valid map pts: %lu\n", pt_array.poses.size()-1);

		//printf("ros_cloud size: %d x %d\n", ros_cloud.height, ros_cloud.width);
		//pub_cloud.publish(ros_cloud);
		pt_array.header.frame_id = "1";
		pt_array.header.seq = frame_id + 1;
		pub_pts_and_pose.publish(pt_array);
		//pub_kf.publish(camera_pose);
	}
}


void printParams() {
	printf("Using params:\n");
	printf("scale_factor: %f\n", scale_factor);
	printf("resize_factor: %f\n", resize_factor);
	printf("cloud_max: %f, %f\t cloud_min: %f, %f\n", cloud_max_x, cloud_max_z, cloud_min_x, cloud_min_z);
	//printf("cloud_min: %f, %f\n", cloud_min_x, cloud_min_z);
	printf("free_thresh: %f\n", free_thresh);
	printf("occupied_thresh: %f\n", occupied_thresh);
	printf("use_local_counters: %d\n", use_local_counters);
	printf("visit_thresh: %d\n", visit_thresh);
	printf("use_gaussian_counters: %d\n", use_gaussian_counters);
	printf("use_boundary_detection: %d\n", use_boundary_detection);
	printf("use_height_thresholding: %d\n", use_height_thresholding);
#ifndef DISABLE_FLANN
	printf("normal_thresh_deg: %f\n", normal_thresh_deg);
#endif
	printf("canny_thresh: %d\n", canny_thresh);
	printf("enable_goal_publishing: %d\n", enable_goal_publishing);
	printf("show_camera_location: %d\n", show_camera_location);
	printf("gaussian_kernel_size: %d\n", gaussian_kernel_size);
	printf("cam_radius: %d\n", cam_radius);
}

