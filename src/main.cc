/**
* This file is part of ORB-SLAM.
*
* Copyright (C) 2014 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <http://webdiis.unizar.es/~raulmur/orbslam/>
*
* ORB-SLAM is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM. If not, see <http://www.gnu.org/licenses/>.
*/

#include<iostream>
#include<fstream>
#include<string>
#include<ros/ros.h>
#include<ros/package.h>
#include<boost/thread.hpp>
#include<opencv2/core/core.hpp>

#include "Tracking.h"
#include "FramePublisher.h"
#include "Map.h"
#include "MapPublisher.h"
#include "LocalMapping.h"
#include "LoopClosing.h"
#include "KeyFrameDatabase.h"
#include "ORBVocabulary.h"
#include "Optimizer.h"

#include "Converter.h"

//hari
#include <sstream>
//

using namespace std;

std::string formatInt(long num, int size) {
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(size) << num;
  return oss.str();
};



int main(int argc, char **argv)
{
    ros::init(argc, argv, "ORB_SLAM");
    ros::start();

    cout << endl << "ORB-SLAM Copyright (C) 2014 Raul Mur-Artal" << endl <<
            "This program comes with ABSOLUTELY NO WARRANTY;" << endl  <<
            "This is free software, and you are welcome to redistribute it" << endl <<
            "under certain conditions. See LICENSE.txt." << endl;

    if(argc != 3)
    {
        cerr << endl << "Usage: rosrun ORB_SLAM ORB_SLAM path_to_vocabulary path_to_settings (absolute or relative to package directory)" << endl;
        ros::shutdown();
        return 1;
    }

    // Load Settings and Check
    string strSettingsFile = ros::package::getPath("ORB_SLAM")+"/"+argv[2];

    cv::FileStorage fsSettings(strSettingsFile.c_str(), cv::FileStorage::READ);
    if(!fsSettings.isOpened())
    {
        ROS_ERROR("Wrong path to settings. Path must be absolut or relative to ORB_SLAM package directory.");
        ros::shutdown();
        return 1;
    }

    //Create Frame Publisher for image_view
    ORB_SLAM::FramePublisher FramePub;

    //Load ORB Vocabulary
   /* Old version to load vocabulary using cv::FileStorage
    string strVocFile = ros::package::getPath("ORB_SLAM")+"/"+argv[1];
    cout << endl << "Loading ORB Vocabulary. This could take a while." << endl;
    cv::FileStorage fsVoc(strVocFile.c_str(), cv::FileStorage::READ);
    if(!fsVoc.isOpened())
    {
        cerr << endl << "Wrong path to vocabulary. Path must be absolut or relative to ORB_SLAM package directory." << endl;
        ros::shutdown();
        return 1;
    }
    ORB_SLAM::ORBVocabulary Vocabulary;
    Vocabulary.load(fsVoc);
    */

    // New version to load vocabulary from text file "Data/ORBvoc.txt".
    // If you have an own .yml vocabulary, use the function
    // saveToTextFile in Thirdparty/DBoW2/DBoW2/TemplatedVocabulary.h
    string strVocFile = ros::package::getPath("ORB_SLAM")+"/"+argv[1];
    cout << endl << "Loading ORB Vocabulary. This could take a while." << endl;

    ORB_SLAM::ORBVocabulary Vocabulary;
    bool bVocLoad = Vocabulary.loadFromTextFile(strVocFile);

    if(!bVocLoad)
    {
        cerr << "Wrong path to vocabulary. Path must be absolut or relative to ORB_SLAM package directory." << endl;
        cerr << "Falied to open at: " << strVocFile << endl;
        ros::shutdown();
        return 1;
    }

    cout << "Vocabulary loaded!" << endl << endl;

    //Create KeyFrame Database
    ORB_SLAM::KeyFrameDatabase Database(Vocabulary);

    //Create the map
    ORB_SLAM::Map World;

    FramePub.SetMap(&World);

    //Create Map Publisher for Rviz
    ORB_SLAM::MapPublisher MapPub(&World);

    //Initialize the Tracking Thread and launch
    ORB_SLAM::Tracking Tracker(&Vocabulary, &FramePub, &MapPub, &World, strSettingsFile);
    boost::thread trackingThread(&ORB_SLAM::Tracking::Run,&Tracker);

    Tracker.SetKeyFrameDatabase(&Database);

    //Initialize the Local Mapping Thread and launch
    ORB_SLAM::LocalMapping LocalMapper(&World);
    boost::thread localMappingThread(&ORB_SLAM::LocalMapping::Run,&LocalMapper);

    //Initialize the Loop Closing Thread and launch
    ORB_SLAM::LoopClosing LoopCloser(&World, &Database, &Vocabulary);
    boost::thread loopClosingThread(&ORB_SLAM::LoopClosing::Run, &LoopCloser);

    //Set pointers between threads
    Tracker.SetLocalMapper(&LocalMapper);
    Tracker.SetLoopClosing(&LoopCloser);

    LocalMapper.SetTracker(&Tracker);
    LocalMapper.SetLoopCloser(&LoopCloser);

    LoopCloser.SetTracker(&Tracker);
    LoopCloser.SetLocalMapper(&LocalMapper);

    //This "main" thread will show the current processed frame and publish the map
    float fps = fsSettings["Camera.fps"];
    if(fps==0)
        fps=30;

    ros::Rate r(fps);

    while (ros::ok())
    {
        FramePub.Refresh();
        MapPub.Refresh();
        Tracker.CheckResetByPublishers();
        r.sleep();
    }

    // Save keyframe poses at the end of the execution
    ofstream f;

    vector<ORB_SLAM::KeyFrame*> vpKFs = World.GetAllKeyFrames();
    sort(vpKFs.begin(),vpKFs.end(),ORB_SLAM::KeyFrame::lId);

    cout << endl << "Saving Keyframe Trajectory to KeyFrameTrajectory.txt" << endl;
    string strFile = ros::package::getPath("ORB_SLAM")+"/"+"KeyFrameTrajectory.txt";
    //string kfstrFile = ros::package::getPath("ORB_SLAM")+"/"+"testframe"+std::to_string(i)+".jpg"; //set path for saving
    f.open(strFile.c_str());
    f << fixed;

    for(size_t i=0; i<vpKFs.size(); i++)
    {
        ORB_SLAM::KeyFrame* pKF = vpKFs[i];

        if(pKF->isBad())
            continue;
/*
        cv::Mat temp= pKF->GetImage();
        string kfstrFile = ros::package::getPath("ORB_SLAM")+"/"; //set path for saving
        ostringstream kfstrFilen;
        kfstrFilen<<kfstrFile<<i<<".jpg";
        cout<<kfstrFilen.str()<<endl;
        cv::imwrite(kfstrFilen.str(),temp);
        cout<<"Saving image"<<i<<endl;
*/
        cv::Mat R = pKF->GetRotation().t();
        vector<float> q = ORB_SLAM::Converter::toQuaternion(R);
        cv::Mat t = pKF->GetCameraCenter();
        f << setprecision(6) << pKF->mTimeStamp << setprecision(7) << " " << t.at<float>(0) << " " << t.at<float>(1) << " " << t.at<float>(2)
          << " " << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << endl;

    }
    f.close();
    ///////-----------------------
    ////   save all the keyframe images
    ///////------------------------
    /*
    string kfstrFile = ros::package::getPath("ORB_SLAM")+"/"+"testframe.jpg"; //set path for saving
    f.open(kfstrFile.c_str()); //open the file handle
    cv::Mat temp=cv::imread("/home/hari/Desktop/my_photo-24.jpg");
    cv::imshow("image",temp);
    cv::imwrite(kfstrFile.c_str(),temp);
    cout<<"Saving image";
    f.close();//close the file handle
    */
    //////////

    //--------------
    //  Export the Poses and the Features to a NVM file
    //--------------
    //adjust the scene first via global BA
    cout << "Starting GlobalBA! This can take some minutes. \n";
    ORB_SLAM::Optimizer::GlobalBundleAdjustemnt(&World,40);

    cout << endl << "Saving NVM to ORB_SLAM.nvm" << endl;
    //See http://ccwu.me/vsfm/doc.html#nvm for details about the file structure
    string nvmStrFile = ros::package::getPath("ORB_SLAM")+"/"+"ORB_SLAM.nvm";
    f.open(nvmStrFile.c_str());
    // fx cx fy cy;
    f << "NVM_V3 \n";
    //used for fixed cameras, but not supported yet by vsfm
    //"_KFIXED " << (double)fsSettings["Camera.fx"] << " " << (double)fsSettings["Camera.fy"] << " " <<
    //    (double)fsSettings["Camera.cx"] << " " << (double)fsSettings["Camera.cy"] << "\n";

    //Now: the model:
    //<Number of cameras>   <List of cameras>
    //<Number of 3D points> <List of points>
    /*
        <Camera> = <Image File name> <focal length> <quaternion WXYZ> <camera center> <radial distortion> 0
        <Point>  = <XYZ> <RGB> <number of measurements> <List of Measurements>
        with:
        <Measurement> = <Image index> <Feature Index> <xy>
    */

    //1.------ Exort the cameras
    //1.1 count the amount of key frames
    int count_good_KF=0;
    for(size_t i=0; i<vpKFs.size(); i++)
    {
        ORB_SLAM::KeyFrame* pKF = vpKFs[i];

        if(pKF->isBad())
            continue;
        count_good_KF+=1;
    }
    cout<<count_good_KF<<"Good KFs";

    f << count_good_KF << "\n"; //now, the list of cameras follows

    //1.2 export the camera parameters itself
    //indexing of key frames by its consecutive number
    std::map<int,int> kf_index;
    int inc_frame_counter=-1;
    for(size_t i=0; i<vpKFs.size(); i++)
    {
        ORB_SLAM::KeyFrame* pKF = vpKFs[i];

        if(pKF->isBad())
            continue;
        inc_frame_counter+=1;

        cv::Mat R = pKF->GetRotation();//.t();
        vector<float> q = ORB_SLAM::Converter::toQuaternion(R);
        cv::Mat t = pKF->GetCameraCenter();
        kf_index[pKF->mnFrameId]=inc_frame_counter;
        //
        cv::Mat temp= pKF->GetImage();
        string kfstrFile = ros::package::getPath("ORB_SLAM")+"/"; //set path for saving
        ostringstream kfstrFilen;
        kfstrFilen<<kfstrFile<<"frame"<<  formatInt(pKF->mnFrameId, 4) << ".jpg " ;
        cout<<kfstrFilen.str()<<endl;
        cv::imwrite(kfstrFilen.str(),temp);
        cout<<"Saving image"<<i<<endl;
        //
        f << "frame"<<  formatInt(pKF->mnFrameId, 4) << ".jpg " << (double)fsSettings["Camera.fx"] << " " <<
            q[3] << " " <<  q[0] << " " << q[1] << " " << q[2] << " " << //WXYZ
            t.at<float>(0) << " " << t.at<float>(1) << " " << t.at<float>(2) << " " <<
            (double)fsSettings["Camera.k1"] << " " << (double)fsSettings["Camera.k2"] << "\n";
    }
    //f<< "\n";
    using namespace ORB_SLAM;
    //2. Export the 3D feature observations
    //<Number of 3D points> <List of points>
    //<Point>  = <XYZ> <RGB> <number of measurements> <List of Measurements>
    //<Measurement> = <Image index> <Feature Index> <xy>

    std::vector<MapPoint*> all_points=World.GetAllMapPoints();
    int count_good_map_points=0;
    for(size_t i=0, iend=all_points.size(); i<iend;i++)
    {
        if (!(all_points[i]->isBad()))
            count_good_map_points+=1;
    }
    cout<<"Good map points"<<count_good_map_points<<endl;
    f << count_good_map_points << "\n";
    for(size_t i=0, iend=all_points.size(); i<iend;i++)
    {
        if (all_points[i]->isBad())
            continue;

        MapPoint* pMP = all_points[i];
        cv::Mat pos=pMP->GetWorldPos();
        f << pos.at<float>(0) << " " << pos.at<float>(1) << " " << pos.at<float>(2) << " " <<
        //rgb
        "0 0 0 ";
        //now all the observations/measurements
        std::map<KeyFrame*,size_t> observations=pMP->GetObservations();
        //count good observations:
        int num_good_observations=0;
        for (std::map<KeyFrame*,size_t>::iterator ob_it=observations.begin(); ob_it!=observations.end(); ob_it++)
        {
            if (!(*ob_it).first->isBad())
                num_good_observations+=1;
        }

        f << num_good_observations << " ";
        for (std::map<KeyFrame*,size_t>::iterator ob_it=observations.begin(); ob_it!=observations.end(); ob_it++)
        {
            //skip if the key frame is "bad"
            if ((*ob_it).first->isBad())
                continue;
            //<Measurement> = <Image index> <Feature Index> <xy>
            std::vector<cv::KeyPoint> key_points=(*ob_it).first->GetKeyPoints();
            f << kf_index[(*ob_it).first->mnFrameId] << " " << (*ob_it).second << " " <<
            key_points[ob_it->second].pt.x << " " <<
            key_points[ob_it->second].pt.y << " ";
        }
        f << "\n";

    }
    f.close();


    ros::shutdown();

	return 0;
}
