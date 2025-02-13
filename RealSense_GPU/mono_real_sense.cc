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

Usage: 

Monocular file 

./build/mono_real_sense Vocabulary/ORBvoc.txt RealSense_GPU/mono_real_sense.yaml


*/


#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>
#include <iomanip>

// include OpenCV header file
#include<opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

#include<opencv2/core/opengl.hpp>

#include<System.h>
#include <Utils.hpp>
#include<Converter.h>

#include <librealsense2/rs.hpp>
#include <cv-helpers.hpp>

using namespace std;


bool operator ! (const cv::Mat &m) {return m.empty();}

int main(int argc, char **argv)
{

    if(argc < 3)
    {
        cerr << endl << "Usage: ./csi_camera path_to_vocabulary path_to_settings" << endl;
        return 1;
    } else if (argc > 7) {
        cerr << endl << "Usage: ./csi_camera path_to_vocabulary path_to_settings" << endl;
        return 1;
    }

    int WIDTH, HEIGHT, FPS;
    double TIME; 
    if (argc > 3) WIDTH = std::atoi(argv[3]); else WIDTH = 640;  //1280
    if (argc > 4) HEIGHT = std::atoi(argv[4]); else HEIGHT = 480; //720 
    if (argc > 5) FPS = std::atoi(argv[5]); else FPS = 30;
    if (argc > 6) TIME = std::atof(argv[6]); else TIME = 30.0;

    //Contruct a pipeline which abstracts the device
    rs2::pipeline pipe;

    //Create a configuration for configuring the pipeline with a non default profile
    rs2::config cfg;

    //Add desired streams to configuration
    cfg.enable_stream(RS2_STREAM_COLOR, WIDTH, HEIGHT, RS2_FORMAT_RGB8, 30);

    //Instruct pipeline to start streaming with the requested configuration
    rs2::pipeline_profile selection = pipe.start(cfg);

    // Camera warmup - dropping several first frames to let auto-exposure stabilize
    rs2::frameset frames;
    for(int i = 0; i < 30; i++)
    {
        //Wait for all configured streams to produce a frame
        frames = pipe.wait_for_frames();
    }

    auto depth_stream = selection.get_stream(RS2_STREAM_COLOR)
                             .as<rs2::video_stream_profile>();
    auto resolution = std::make_pair(depth_stream.width(), depth_stream.height());
    auto i = depth_stream.get_intrinsics();
    auto principal_point = std::make_pair(i.ppx, i.ppy);
    auto focal_length = std::make_pair(i.fx, i.fy);

    //std::cout << "Width: " << resolution[0] << "Height: " << resolution[1] << std::endl;
    std::cout << "ppx: " << i.ppx << " ppy: " << i.ppy << std::endl;
    std::cout << "fx: " << i.fx << " fy: " << i.fy << std::endl;
    std::cout << "k1: " << i.coeffs[0] << " k2: " << i.coeffs[1] << " p1: " << i.coeffs[2] << " p2: " << i.coeffs[3] << " k3: " << i.coeffs[4] << std::endl;

    bool bUseViz = true;

    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::MONOCULAR,bUseViz);


    cout << endl << "-------" << endl;
    cout << "Start processing sequence ..." << endl;


    double tsum = 0;
    double tbuf[10] = {0.0};
    int tpos = 0;
    double trackTimeSum = 0.0;
    // Main loop
    cv::Mat im;

    SET_CLOCK(t0);
    int frameNumber = 0;
    while (true) {

      //Get each frame
      frames = pipe.wait_for_frames();

      SET_CLOCK(t1);

      rs2::video_frame color = frames.first(RS2_STREAM_COLOR);
      cv::Mat dMat_left = cv::Mat(cv::Size(WIDTH, HEIGHT), CV_8UC3, (void*)color.get_data());
      
      double tframe = TIME_DIFF(t1, t0);
      if (tframe > TIME) {
        break;
      }

      PUSH_RANGE("Track image", 4);
      // Pass the image to the SLAM system
      cv::Mat Tcw = SLAM.TrackMonocular(dMat_left,tframe);

      POP_RANGE;
      SET_CLOCK(t2);

      double trackTime = TIME_DIFF(t2, t1);
      trackTimeSum += trackTime;
      tsum = tframe - tbuf[tpos];
      tbuf[tpos] = tframe;
      tpos = (tpos + 1) % 10;
      //cerr << "Frame " << frameNumber << " : " << tframe << " " << trackTime << " " << 10 / tsum << "\n";
      ++frameNumber;

      // Publish the pose information to a ROS node 
      /*
      if (!Tcw == false)
      {
          geometry_msgs::PoseStamped pose;
          pose.header.stamp = fTime;
          pose.header.frame_id ="map";

          cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t(); // Rotation information
          cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3); // translation information
          vector<float> q = ORB_SLAM2::Converter::toQuaternion(Rwc);

          tf::Transform new_transform;
          new_transform.setOrigin(tf::Vector3(twc.at<float>(0, 0), twc.at<float>(0, 1), twc.at<float>(0, 2)));

          tf::Quaternion quaternion(q[0], q[1], q[2], q[3]);
          new_transform.setRotation(quaternion);

          tf::poseTFToMsg(new_transform, pose.pose);
          pose_pub.publish(pose);
	  std::cout << "Position: x: " << twc.at<float>(0, 0) << ", y: " << twc.at<float>(0, 1) << ", z: " << twc.at<float>(0, 2) << std::endl;
	  std::cout << "Quaternion: " << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << std::endl;
      }
      */
    }

    cerr << "Mean track time: " << trackTimeSum / frameNumber << " , mean fps: " << frameNumber / TIME << "\n";

    // Stop all threads
    SLAM.Shutdown();




    // Save camera trajectory
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");

    return 0;
}

