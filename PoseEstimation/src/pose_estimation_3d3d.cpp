#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/SVD>
#include <g2o/core/base_vertex.h>
#include <g2o/core/base_unary_edge.h>
#include <g2o/core/block_solver.h>
#include <g2o/core/optimization_algorithm_gauss_newton.h>
#include <g2o/solvers/eigen/linear_solver_eigen.h>
#include <g2o/types/sba/types_six_dof_expmap.h>
#include <chrono>
#include <boost/concept_check.hpp>

using namespace std;
using namespace cv;

void find_feature_matches(
   const Mat& img_1,const Mat& img_2,
   std::vector<KeyPoint>& keypoints_1,
   std::vector<KeyPoint>& keypoints_2,
   std::vector <DMatch>& matches );

Point2d pixel2cam(const Point2d& p,const Mat& K);

void pose_estimation_3d3d(
   const vector<Point3f>& pts1,
   const vector<Point3f>& pts2,
   Mat& R,Mat& t );

void bundleAdjustment(
   const vector<Point3f>& pts1,
   const vector<Point3f>& pts2,
   Mat& R, Mat& t );

class EdgeProjectXYZRGBDPoseOnly: public g2o::BaseUnaryEdge<3,Eigen::Vector3d,g2o::VertexSE3Expmap>
{
public:
   EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
   EdgeProjectXYZRGBDPoseOnly(const Eigen::Vector3d& point):_point(point){}

   virtual void computeError()
   {
      const g2o::VertexSE3Expmap* pose = static_cast<const g2o::VertexSE3Expmap*> (_vertices[0]);
      // measurement is p, point is p'
      _error = _measurement - pose->estimate().map(_point);
   }

   virtual void linearlizeOplus()
   {
      g2o::VertexSE3Expmap* pose = static_cast<g2o::VertexSE3Expmap *>(_vertices[0]);
      g2o::SE3Quat T(pose->estimate());
      Eigen::Vector3d xyz_trans = T.map(_point);
      double x = xyz_trans[0];
      double y = xyz_trans[1];
      double z = xyz_trans[2];

      _jacobianOplusXi(0,0) = 0;
      _jacobianOplusXi(0,1) = -z;
      _jacobianOplusXi(0,2) = y;
      _jacobianOplusXi(0,3) = -1;
      _jacobianOplusXi(0,4) = 0;
      _jacobianOplusXi(0,5) = 0;

      _jacobianOplusXi(1,0) = z;
      _jacobianOplusXi(1,1) = 0;
      _jacobianOplusXi(1,2) = -x;
      _jacobianOplusXi(1,3) = 0;
      _jacobianOplusXi(1,4) = -1;
      _jacobianOplusXi(1,5) = 0;

      _jacobianOplusXi(2,0) = -y;
      _jacobianOplusXi(2,1) = x;
      _jacobianOplusXi(2,2) = 0;
      _jacobianOplusXi(2,3) = 0;
      _jacobianOplusXi(2,4) = 0;
      _jacobianOplusXi(2,5) = -1;
   }

   bool read (istream& in) {}
   bool write (ostream& out) const {}

protected:
   Eigen::Vector3d _point;
};

int main (int argc,char** argv)
{
   if(argc != 5)
   {
      cout<<"usage: pose_estimation_3d3d img_1 img_2 depth_1 depth2 "<<endl;
      return 1;
   }

   Mat img_1 = imread (argv[1],CV_LOAD_IMAGE_COLOR);
   Mat img_2 = imread (argv[2],CV_LOAD_IMAGE_COLOR);

   vector<KeyPoint> keypoints_1,keypoints_2;
   vector<DMatch> matches;
   find_feature_matches(img_1,img_2,keypoints_1,keypoints_2,matches);
   cout<<"The size of matches is "<<matches.size()<<endl;

   Mat depth_1 = imread(argv[3],CV_LOAD_IMAGE_UNCHANGED);
   Mat depth_2 = imread(argv[4],CV_LOAD_IMAGE_UNCHANGED);

   Mat K = (Mat_<double>(3,3) << 520.9, 0, 325.1, 0, 521.0, 249.7, 0, 0, 1 );
   vector<Point3f> pts1,pts2;

   for (DMatch m:matches)
   {
      ushort d1 = depth_1.ptr<unsigned short> (int (keypoints_1[m.queryIdx].pt.y))[int (keypoints_1[m.queryIdx].pt.x)];
      ushort d2 = depth_2.ptr<unsigned short> (int (keypoints_2[m.queryIdx].pt.y))[int (keypoints_2[m.trainIdx].pt.x)];
      if (d1 == 0 || d2 == 0)
	 continue;
      Point2d p1 = pixel2cam(keypoints_1[m.queryIdx].pt,K);
      Point2d p2 = pixel2cam(keypoints_2[m.queryIdx].pt,K);
      float _d1 = float(d1)/1000.0;
      float _d2 = float(d2)/1000.0;
      pts1.push_back(Point3f(p1.x*_d1,p1.y*_d2,_d1));
      pts2.push_back(Point3f(p2.x*_d2,p2.y*_d2,_d2));

   }

   cout<<"3d-3d pairs: "<<pts1.size()<<endl;
   Mat R,t;
   pose_estimation_3d3d(pts1,pts2,R,t);

   cout<<"ICP via SVD result :"<<endl<<"R = "<<R<<endl<<"t = "<<endl;
   cout<<"R_inv = "<< endl << R.t()<<endl;
   cout<<"t_inv = "<< endl << -R.t()*t<<endl;

   cout<<"Running bundleAdjustment"<<endl;

   bundleAdjustment(pts1,pts2,R,t);

   // verify p1 = R*p2 + t;
   for(int i = 0;i<5;i++)
   {
      cout<<"p1 = "<<pts1[i]<<endl;
      cout<<"p2 = "<<pts2[i]<<endl;
      cout<<"R*p2+t = "<< R*(Mat_<double>(3,1)<<pts2[i].x,pts2[i].y,pts2[i].z)+t<<endl<<endl;
   }
}

void find_feature_matches(
   const Mat& img_1,const Mat& img_2,
   std::vector<KeyPoint>& keypoints_1,
   std::vector<KeyPoint>& keypoints_2,
   std::vector <DMatch>& matches )
{
   // Step1: Initializing
   Mat descriptors_1,descriptors_2;
   
   Ptr<FeatureDetector> detector = ORB::create();
   Ptr<DescriptorExtractor> descriptor = ORB::create();
   Ptr<DescriptorMatcher> matcher = DescriptorMatcher::create("BruthForce-Hamming");

   // Step2: detect Oriented FAST corner and compute descriptor;
   detector->detect(img_1,keypoints_1);
   detector->detect(img_2,keypoints_2);
   descriptor->compute(img_1,keypoints_1,descriptors_1);
   descriptor->compute(img_2,keypoints_2,descriptors_2);

   // Step3: match descriptors;
   vector<DMatch> match;
   matcher->match(descriptors_1,descriptors_2,match);

   // Step4: Filter the error match pairs
   double min_dist =1000,max_dist = 0;
   for(int i = 0;i<descriptors_1.rows;i++)
   {
      double dist = match[i].distance;
      if(dist>max_dist) max_dist = dist;
      if(dist<min_dist) min_dist = dist;
   }
   cout<<"max dist: "<<max_dist<<endl;
   cout<<"min_dist: "<<min_dist<<endl;

   for(int i = 0;i<descriptors_1.rows;i++)
   {
      if(match[i].distance <= max(2*min_dist,30.0))
	 matches.push_back(match[i]);
   }
}

Point2d pixel2cam ( const Point2d& p, const Mat& K )
{
   return Point2d ( ( p.x - K.at<double> ( 0,2 ) ) / K.at<double> ( 0,0 ),( p.y - K.at<double> ( 1,2 ) ) / K.at<double> ( 1,1 )  );

}

void pose_estimation_3d3d(const vector< Point3f >& pts1, const vector< Point3f >& pts2, Mat& R, Mat& t)
{
   int n = pts1.size();
   // Step1:compute the mean of 3d points pairs;
   Point3f p1_mean , p2_mean ;
   for(int i = 0;i < n;i++)
   {
      p1_mean += pts1[i];
      p2_mean += pts2[i];
   }
   p1_mean /= n,   p2_mean /= n;

   //Step2:remove centroid point;
   std::vector<Point3f> q1(n) ,q2(n);//
   for(int i = 0;i<n;i++)
   {
      // 利用vector下标进行赋值操作；
      q1[i] = (pts1[i]-p1_mean);
      q2[i] = (pts2[i]-p2_mean);
   }

   //Step3:compute the W matrix and SVD W;
   Eigen::Matrix3d W = Eigen::Matrix3d::Zero();
   for (int i = 0; i<n;i++)
   {
      W += Eigen::Vector3d(q1[i].x,q1[i].y,q1[i].z) * Eigen::Vector3d(q2[i].x,q2[i].y,q2[i].z).transpose();
   }
   cout<<"W = "<< endl << W << endl;
   Eigen::JacobiSVD<Eigen::Matrix3d> svd (W,Eigen::ComputeFullU|Eigen::ComputeFullV);
   Eigen::Matrix3d U = svd.matrixU();
   Eigen::Matrix3d V = svd.matrixV();
   cout<<"U = "<<endl<<U<<endl;
   cout<<"V = "<<endl<<V<<endl;

   //Step4:compute R and t
   Eigen::Matrix3d R_ = U*(V.transpose());
   Eigen::Vector3d t_ = Eigen::Vector3d(p1_mean.x,p1_mean.y,p1_mean.z)-R_ * Eigen::Vector3d(p2_mean.x,p2_mean.y,p2_mean.z);

   // make R and t convert to cv::Mat from Eigen::Matrix
   R = (Mat_<double> (3,3) <<
	 R_(0,0),R_(0,1),R_(0,2),
	 R_(1,0),R_(1,1),R_(1,2),
	 R_(2,0),R_(2,1),R_(2,2)
      );
   t = (Mat_<double> (3,1) <<t_(0,0),t_(1,0),t_(2,0));
}

void bundleAdjustment(const vector< Point3f >& pts1, const vector< Point3f >& pts2, Mat& R, Mat& t)
{
   // Step1: Initializing g2o
   typedef g2o::BlockSolver<g2o::BlockSolverTraits<6,3> > Block;
   Block::LinearSolverType* linearSolver = new g2o::LinearSolverEigen<Block::PoseMatrixType>();
   Block* solver_ptr = new Block(linearSolver);
   g2o::OptimizationAlgorithmGaussNewton* solver = new g2o::OptimizationAlgorithmGaussNewton(solver_ptr);
   g2o::SparseOptimizer optimizer;
   optimizer.setAlgorithm(solver);

   // Step2: Set vertex
   // Camera pose
   g2o::VertexSE3Expmap* pose = new g2o::VertexSE3Expmap();
   pose->setId(0);
   pose->setEstimate(g2o::SE3Quat(Eigen::Matrix3d::Identity(),Eigen::Vector3d(0,0,0)));
   optimizer .addVertex(pose);

   //Step3:Set edges
   int index = 1;
   vector<EdgeProjectXYZRGBDPoseOnly*> edges;
   for(size_t i = 0;i< pts1.size();i++)
   {
      EdgeProjectXYZRGBDPoseOnly* edge = new EdgeProjectXYZRGBDPoseOnly(Eigen::Vector3d(pts2[i].x,pts2[i].y,pts2[i].z));
      edge->setId(index);
      edge->setVertex(0,dynamic_cast<g2o::VertexSE3Expmap*> (pose));
      edge->setMeasurement(Eigen::Vector3d(pts1[i].x,pts1[i].y,pts1[i].z));
      edge->setInformation(Eigen::Matrix3d::Identity()*1e4);
      optimizer .addEdge(edge);
      index++;
      edges.push_back(edge);
   }

   chrono::steady_clock::time_point t1 = chrono::steady_clock::now();
   optimizer.setVerbose(true);
   optimizer.initializeOptimization();
   optimizer.optimize(10);
   chrono::steady_clock::time_point t2 = chrono::steady_clock::now();
   chrono::duration<double> time_used = chrono::duration_cast<chrono::duration<double>>(t2-t1);
   cout<<"optimization costs time: "<<time_used.count()<<" seconds."<<endl;
   cout<<endl<<"after optimization:"<<endl;
   cout<<"T="<<endl<<Eigen::Isometry3d( pose->estimate() ).matrix()<<endl;
}
