/*************************************************************************
    > File Name: frame.cpp
    > Author: ll.pan
    > Mail: ll.pan931204@gmail.com 
    > Created Time: Mon 17 Jul 2017 04:36:00 PM CST
 ************************************************************************/

#include "myslam/frame.h"

namespace myslam
{ 
  Frame::Frame():id_(-1),time_stamp_(-1),camera_(nullptr){}
  Frame::Frame(long id, double time_stamp, SE3 T_cw, Camera::Ptr camera, Mat color, Mat depth)
	  :id_(id), time_stamp_(time_stamp), T_cw_(T_cw), camera_(camera), color_(color), depth_(depth){}

  Frame::~Frame(){}
  Frame::Ptr Frame::createFrame()
  {
    static long factory_id = 0;
    return Frame::Ptr(new Frame(factory_id++));
  }
  double Frame::findDepth(const cv::KeyPoint& kp)
  {
    int x = cvRound(kp.pt.x);
    int y = cvRound(kp.pt.y);
    ushort d = depth_.ptr<ushort>(y)[x];
    if(d != 0)
    {
	return double(d)/camera_->depth_scale_;
    } else {
	// check the nearby points
	int dx[4] = {-1,0,1,0};
	int dy[4] = {0,-1,0,1};
	for(int i = 0;i<4;i++)
	{
	  d = depth_.ptr<ushort>(y + dy[i])[x + dx[i]];
	  if (d != 0)
	  {
	    return double(d)/camera_->depth_scale_;
	  }
	}
    }
    return -1.0;
  }
  
  Vector3d Frame::getCamCenter() const
  {
    return T_cw_.inverse().translation();
  }
  
  bool Frame::isInFrame (const Vector3d& pt_world)
  {
    Vector3d p_cam = camera_->world2camera(pt_world,T_cw_);
    if(p_cam(2,0) < 0)
	return false;
    Vector2d pixel = camera_->world2pixel(pt_world,T_cw_);
    return pixel(0,0) > 0 
    && pixel(1,0) > 0
    && pixel(0,0) < color_.cols 
    && pixel(1,0) < color_.rows;
  }
}
