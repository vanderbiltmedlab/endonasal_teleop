/********************************************************************

  resolved_rates.cpp

Resolved rates node for teleoperation of endonasal system.
Subscribes to messages from haptic device and kinematics.
Publishes desired joint movement messages.

Andria Remirez & Lin Liu
revised:  6/14/2018
********************************************************************/


// Qt headers
#include <QCoreApplication>
#include <QVector>

// Cannula kinematics headers
#include "Kinematics.h"
#include "BasicFunctions.h"
#include "Tube.h"

// Eigen headers
#include <Eigen/Dense>
#include <Eigen/Geometry>

// ROS headers
#include <ros/ros.h>
#include <ros/console.h>

//XML parsing headers
#include "rapidxml.hpp"

// Message & service headers
#include <tf_conversions/tf_eigen.h>
#include <geometry_msgs/Pose.h>
#include <std_msgs/Int32.h>
#include "std_msgs/Int8.h"
#include "std_msgs/MultiArrayLayout.h"
#include "std_msgs/MultiArrayDimension.h"
#include "std_msgs/Float64MultiArray.h"
#include "std_msgs/Float64.h"
#include "std_msgs/Bool.h"
#include <endonasal_teleop/matrix6.h>
#include <endonasal_teleop/matrix8.h>
#include <endonasal_teleop/config3.h>
#include <endonasal_teleop/vector7.h>
#include <endonasal_teleop/kinout.h>
#include <endonasal_teleop/getStartingConfig.h>
#include <endonasal_teleop/getStartingKin.h>
#include <geometry_msgs/Vector3.h>

#include "medlab_motor_control_board/McbEncoders.h"

// Misc. headers
#include <Mtransform.h>
#include <bits/stdc++.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "spline.h"
#include <iostream>
#include <fstream>
#include <random>
#include <vector>
#include <cmath>

// NAMESPACES
using namespace rapidxml;
using namespace Eigen;
using namespace CTR;
using namespace CTR::Functions;
using std::tuple;
using namespace std;

// TYPEDEFS
typedef Eigen::Matrix<double,4,4> Matrix4d;
typedef Eigen::Matrix<double,6,6> Matrix6d;
typedef Eigen::Matrix<double,7,1> Vector7d;
typedef Eigen::Matrix<double,6,1> Vector6d;
typedef tuple < Tube< constant_fun< Vector2d > >,
Tube< constant_fun< Vector2d > >,
Tube< constant_fun< Vector2d > > > Cannula3;
typedef constant_fun<Eigen::Vector2d> CurvFun;
typedef std::tuple< Tube<CurvFun>, Tube<CurvFun>, Tube<CurvFun> > CannulaT;
typedef DeclareOptions< Option::ComputeJacobian, Option::ComputeGeometry, Option::ComputeStability, Option::ComputeCompliance>::options OType;

struct Configuration3
{
    Eigen::Vector3d	PsiL;
    Eigen::Vector3d	Beta;
    Eigen::Vector3d     Ftip;
    Eigen::Vector3d	Ttip;
};

// GLOBAL VARIABLES NEEDED FOR RESOLVED RATES
Matrix4d omniPose;
Matrix4d prevOmni;
Matrix4d curOmni;
Matrix4d robotTipFrameAtClutch; //clutch-in position of cannula

geometry_msgs::Vector3 omniForce;

Eigen::Vector3d ptipcur; // use for continually updated message value
Eigen::Vector4d qtipcur;
Eigen::Vector3d alphacur;
Matrix6d Jcur;
bool new_kin_msg = 0;
double rosLoopRate = 100.0;

// BASIC MATH FUNCTION DEFINITIONS -----------------------------------

double deg2rad (double degrees) {
    return degrees * 4.0 * atan (1.0) / 180.0;
}

double vectornorm(Eigen::Vector3d v)
{
    double n = sqrt(v.transpose()*v);
    return n;
}

Eigen::Matrix3d orthonormalize(Eigen::Matrix3d R)
{
    Eigen::Matrix3d R_ortho;
    R_ortho.fill(0);
    // Normalize the first column:
    R_ortho.col(0) = R.col(0) / vectornorm(R.col(0));

    // Orthogonalize & normalize second column:
    R_ortho.col(1) = R.col(1);
    double c = (R_ortho.col(1).transpose()*R_ortho.col(0));
    c = c/(R_ortho.col(0).transpose()*R_ortho.col(0));
    R_ortho.col(1) = R_ortho.col(1) - c*R_ortho.col(0);
    R_ortho.col(1) = R_ortho.col(1)/vectornorm(R_ortho.col(1));

    // Orthogonalize & normalize third column:
    R_ortho.col(2) = R.col(2);
    double d = (R_ortho.col(2).transpose()*R_ortho.col(0));
    d = d/(R_ortho.col(0).transpose()*R_ortho.col(0));
    R_ortho.col(2) = R_ortho.col(2) - d*R_ortho.col(0);
    double e = (R_ortho.col(2).transpose()*R_ortho.col(1));
    e = e/(R_ortho.col(1).transpose()*R_ortho.col(1));
    R_ortho.col(2) = R_ortho.col(2) - e*R_ortho.col(1);
    R_ortho.col(2) = R_ortho.col(2)/vectornorm(R_ortho.col(2));
    return R_ortho;
}

Eigen::Matrix4d assembleTransformation(Eigen::Matrix3d Rot, Eigen::Vector3d Trans)
{
    Rot = orthonormalize(Rot);
    Eigen::Matrix4d T;
    T.fill(0);
    T.topLeftCorner(3,3) = Rot;
    T.topRightCorner(3,1) = Trans;
    T(3,3) = 1;
    return T;
}

Eigen::Matrix3d quat2rotm(Eigen::Vector4d Quat)
{
    // Agrees with Matlab
    // Quaternion order is wxyz
    Eigen::Matrix3d R;
    R.fill(0);

    R(0,0) = pow(Quat(0),2) + pow(Quat(1),2) - pow(Quat(2),2) - pow(Quat(3),2);
    R(0,1) = 2*Quat(1)*Quat(2) - 2*Quat(0)*Quat(3);
    R(0,2) = 2*Quat(1)*Quat(3) + 2*Quat(0)*Quat(2);

    R(1,0) = 2*Quat(1)*Quat(2) + 2*Quat(0)*Quat(3);
    R(1,1) = pow(Quat(0),2) - pow(Quat(1),2) + pow(Quat(2),2) - pow(Quat(3),2);
    R(1,2) = 2*Quat(2)*Quat(3) - 2*Quat(0)*Quat(1);

    R(2,0) = 2*Quat(1)*Quat(3) - 2*Quat(0)*Quat(2);
    R(2,1) = 2*Quat(2)*Quat(3) + 2*Quat(0)*Quat(1);
    R(2,2) = pow(Quat(0),2) - pow(Quat(1),2) - pow(Quat(2),2) + pow(Quat(3),2);
    return R;
}


// Function to get cofactor of A[p][q] in temp[][]
void getCofactor(double A[6][6], double temp[6][6], int p, int q, int n)
{
    int i = 0, j = 0;

    // Looping for each element of the matrix
    for (int row = 0; row < n; row++)
    {
        for (int col = 0; col < n; col++)
        {
            //  Copying into temporary matrix only those element
            //  which are not in given row and column
            if (row != p && col != q)
            {
                temp[i][j++] = A[row][col];

                // Row is filled, so increase row index and
                // reset col index
                if (j == n - 1)
                {
                    j = 0;
                    i++;
                }
            }
        }
    }
}

// Recursive function for finding determinant of matrix.
double determinant(double A[6][6], int n)
{
    double D = 0; // Initialize result

    //  Base case : if matrix contains single element
    if (n == 1)
        return A[0][0];

    double temp[6][6]; // To store cofactors

    int sign = 1;  // To store sign multiplier

    // Iterate for each element of first row
    for (int f = 0; f < n; f++)
    {
        // Getting Cofactor of A[0][f]
        getCofactor(A, temp, 0, f, n);
        D += sign * A[0][f] * determinant(temp, n - 1);

        // terms are to be added with alternate sign
        sign = -sign;
    }

    return D;
}

// Function to get adjoint of A[N][N] in adj[N][N].
void adjoint(double A[6][6],double adj[6][6])
{
    // temp is used to store cofactors of A[][]
    int sign = 1;
    double temp[6][6];

    for (int i=0; i<6; i++)
    {
        for (int j=0; j<6; j++)
        {
            // Get cofactor of A[i][j]
            getCofactor(A, temp, i, j, 6);

            // sign of adj[j][i] positive if sum of row
            // and column indexes is even.
            sign = ((i+j)%2==0)? 1: -1;

            // Interchanging rows and columns to get the
            // transpose of the cofactor matrix
            adj[j][i] = (sign)*(determinant(temp, 6-1));
        }
    }
}

// Function to calculate and store inverse, returns false if
// matrix is singular
void inverse(double A[6][6], double inverse[6][6])
{
    // Find determinant of A[][]
    double det = determinant(A, 6);
    if (det != 0) //matrix is not singular
    {
        // Find adjoint
        double adj[6][6];
        adjoint(A, adj);

        // Find Inverse using formula "inverse(A) = adj(A)/det(A)"
        for (int i=0; i<6; i++)
            for (int j=0; j<6; j++)
                inverse[i][j] = adj[i][j]/double(det);
    }
}

// 
Vector6d saturateJointVelocities(Vector6d delta_qx, int node_freq)
{

double max_rot_speed = 0.8; // rad/sec
double max_trans_speed = 5.0; // mm/sec

Vector6d commanded_speed = delta_qx*node_freq;
Vector6d saturated_speed = commanded_speed;
Vector6d delta_qx_sat;

// saturate tip rotations
for (int i=0; i<3; i++)
{
    if(fabs(commanded_speed(i)) > max_rot_speed)
    {
        saturated_speed(i) = (commanded_speed(i)/fabs(commanded_speed(i)))*max_rot_speed;
		std::cout << "Tube rotation speed saturated for tube" << i << std::endl << std::endl;
    }
    delta_qx_sat(i) = saturated_speed(i)/node_freq;
}

// saturate translations
for (int i=3; i<6; i++)
{
    if(fabs(commanded_speed(i)) > max_trans_speed)
    {
        saturated_speed(i) = (commanded_speed(i)/fabs(commanded_speed(i)))*max_trans_speed;
		std::cout << "Tube translation speed saturated for tube" << i << std::endl << std::endl;
    }
    delta_qx_sat(i) = saturated_speed(i)/node_freq;
}

return delta_qx_sat;

}

Vector6d transformBetaToX(Vector6d qbeta, Eigen::Vector3d L)
{
    Vector6d qx;
    qx << qbeta(0), qbeta(1), qbeta(2), 0, 0, 0;
    qx(3) = L(0) - L(1) + qbeta(3) - qbeta(4);
    qx(4) = L(1) - L(2) + qbeta(4) - qbeta(5);
    qx(5) = L(2) + qbeta(5);
    return qx;
}

Vector6d transformXToBeta(Vector6d qx, Eigen::Vector3d L)
{
    Vector6d qbeta;
    qbeta << qx(0), qx(1), qx(2), 0, 0, 0;
    qbeta(3) = qx(3) + qx(4) + qx(5) - L(0);
    qbeta(4) = qx(4) + qx(5) - L(1);
    qbeta(5) = qx(5) - L(2);
    return qbeta;
}

double dhFunction(double xmin, double xmax, double x)
{
    double dh = fabs((xmax-xmin)*(xmax-xmin)*(2*x-xmax-xmin)/(4*(xmax-x)*(xmax-x)*(x-xmin)*(x-xmin)));

    return dh;
}

struct weightingRet
{
    Eigen::Matrix<double,6,6>   W;
    Eigen::Vector3d             dh;
};

weightingRet getWeightingMatrix(Eigen::Vector3d x, Eigen::Vector3d dhPrev, Eigen::Vector3d L, double lambda)
{
    Eigen::Matrix<double,6,6> W = MatrixXd::Identity(6,6);

    // No penalties on the rotational degrees of freedom (they don't have any joint limits)
    // Therefore leave the first three entries in W as 1.

    double eps = 2e-3;

    // x1:
    double x1min = eps;
    double x1max = L(0)-L(1)-eps;
    double x1 = x(0);
    double dh1 = dhFunction(x1min,x1max,x1);
    W(3,3) = (dh1 >= dhPrev(0))*(1+dh1) + (dh1 < dhPrev(0))*1;
    //W(3,3) = 1+dh1;
    W(3,3) *= lambda;

    // x2:
    double x2min = eps;
    double x2max = L(1)-L(2)-eps;
    double x2 = x(1);
    double dh2 = dhFunction(x2min,x2max,x2);
    W(4,4) = (dh2 >= dhPrev(1))*(1+dh2) + (dh2 < dhPrev(1))*1;
    //W(4,4) = 1+dh2;
    W(4,4) *= lambda;

    // x3:
    double x3min = eps;
    double x3max = L(2)-eps;
    double x3 = x(2);
    double dh3 = dhFunction(x3min,x3max,x3);
    W(5,5) = (dh3 >= dhPrev(2))*(1+dh3) + (dh3 < dhPrev(2))*1;
    //W(5,5) = 1+dh3;
    W(5,5) *= lambda;

    Eigen::Vector3d dh;
    dh << dh1,dh2,dh3;

    weightingRet output;
    output.W = W;
    output.dh = dh;
    return output;
}


Eigen::Vector3d limitBetaValsSimple(Eigen::Vector3d x_in, Eigen::Vector3d L)
{
    Eigen::Vector3d x = x_in;
    double epsilon = 0.5e-3;  // keep a 0.5 mm minimum margin

    // check tube 3 first:
    if (x(2) < epsilon)
    {
        x(2) = epsilon;
        std::cout << "Tube 3 translation saturated (front)" << std::endl;
    }
    else if (x(2) > L(2)-epsilon)
    {
        x(2) = L(2)-epsilon;
        std::cout << "Tube 3 translation saturated (rear)" << std::endl;
    }

    // now check tube 2:
    if (x(1) < epsilon)
    {
        x(1) = epsilon;
        std::cout << "Tube 2 translation saturated (front)" << std::endl;
    }
    else if (x(1) > L(1)-L(2)-epsilon)
    {
        x(1) = L(1)-L(2)-epsilon;
        std::cout << "Tube 2 translation saturated (rear)" << std::endl;
    }

    // and last check tube 1:
    if (x(0) < epsilon)
    {
        x(0) = epsilon;
        std::cout << "Tube 1 translation saturated (front)" << std::endl;
    }
    else if (x(0) > L(0)-L(1)-epsilon)
    {
        x(0)=L(0)-L(1)-epsilon;
        std::cout << "Tube 1 translation saturated (rear)" << std::endl;
    }

    return x;
}

Eigen::Vector3d limitBetaValsBimanualAlgorithm(Eigen::Vector3d Beta_in, Eigen::Vector3d L_in)
{
    int nTubes = 3;

    // plate thicknesses (constant)
    // eventually we'll want to load them from elsewhere
    // these also need to be updated to correct values for the new robot
    // and this whole algorithm will need to be adapted
    Eigen::Matrix<double,4,1> tplus;
    tplus << 0.0, 10e-3, 10e-3, 10e-3;

    Eigen::Matrix<double,4,1> tminus;
    tminus << 0.0, 10e-3, 10e-3, 10e-3;

    Eigen::Matrix<double,4,1> extMargin;
    extMargin << 1e-3, 1e-3, 10e-3, 1e-3;
    // tube 2 is different because we have to make sure it doesn't knock off the end effector

    double tp = 25e-3;

    Eigen::Matrix<double,4,1> L;
    L << 3*L_in(0), L_in(0), L_in(1), L_in(2);

    Eigen::Matrix<double,4,1> beta;
    beta << -2*L_in(0), Beta_in(0), Beta_in(1), Beta_in(2);

    // starting with tube 1, correct any translational joint limit violations
    for (int i = 1; i <=nTubes; i++)
    {
        // if it's going to hit the carriage behind it, move it up:
        if (beta(i) - tminus(i) < beta(i-1) + tplus(i-1))
        {
            beta(i) = beta(i-1) + tplus(i-1) + tminus(i);
        }

        // if it's going to not leave space for the other carriages in front of it, move it back:
        double partial_sum = 0.0;
        for (int k = i+1; k <= nTubes; k++)
        {
            partial_sum += tplus(k) + tminus(k);
        }
        if (beta(i) + tplus(i) > -tp - partial_sum)
        {
            beta(i) = -tp - partial_sum - tplus(i);
        }

        // if the tube is getting too close to the end of the next tube out, move it up:
        if (beta(i) + L(i) > beta(i-1) + L(i-1) - extMargin(i))
        {
            beta(i) = beta(i-1) + L(i-1) - L(i) - extMargin(i);
        }

        // if the tube is going to retract all the way behind the front plate, move it up:
        if (beta(i) + L(i) - 0.001*(nTubes-i+1) < 0.0)
        {
            beta(i) = -L(i) + 0.001*(nTubes-i+1);
        }
    }

    Eigen::Vector3d Beta;
    Beta << beta(1), beta(2), beta(3);
    return Beta;
}

Vector6d scaleOmniVelocity(Vector6d desTwistDelta)
{
	Vector6d scaledDesTwistDelta = desTwistDelta;

	//double pStepMax = 2.0e-3; // 2 mm
	double vMax = 0.1; // [m/s]
	double pStepMax = vMax / rosLoopRate; // [mm]

	double pErr = desTwistDelta.topRows<3>().norm();
	double gain = 1.0;
	if (pErr > pStepMax)
	{
		gain = 	pStepMax / pErr;
	}
	scaledDesTwistDelta.topRows<3>() *= gain;

	//double angleStepMax = 0.05; //0.05 radians
	double wMax = 2.5; // [rad/s]
	double angleStepMax = wMax / rosLoopRate;
	double angleErr = desTwistDelta.bottomRows<3>().norm();
	gain = 1.0;
	if (angleErr > angleStepMax)
	{
		gain = angleStepMax / angleErr;
	}
	scaledDesTwistDelta.bottomRows<3>() *= gain;

	return scaledDesTwistDelta;
}


// SERVICE CALL FUNCTION DEFINITION ------------------------------

bool startingConfig(endonasal_teleop::getStartingConfig::Request &req, endonasal_teleop::getStartingConfig::Response &res)
{
    std::cout << "Retrieving the starting configuration..." << std::endl << std::endl;

    Configuration3 qstart;
    qstart.PsiL = Eigen::Vector3d::Zero();
    qstart.Beta << -160.9e-3, -127.2e-3, -86.4e-3;
    qstart.Ftip = Eigen::Vector3d::Zero();
    qstart.Ttip = Eigen::Vector3d::Zero();

    for(int i = 0; i<3; i++)
    {
        res.joint_q[i] = qstart.PsiL[i];
        res.joint_q[i+3] = qstart.Beta[i];
        res.joint_q[i+6] = qstart.Ftip[i];
        res.joint_q[i+9] = qstart.Ttip[i];
    }

    return true;
}


// MESSAGE CALLBACK FUNCTION DEFINITIONS ---------------------------

endonasal_teleop::kinout tmpkin;
void kinCallback(const endonasal_teleop::kinout kinmsg)
{
    tmpkin = kinmsg;

    // pull out position
    ptipcur[0] = tmpkin.p[0];
    ptipcur[1] = tmpkin.p[1];
    ptipcur[2] = tmpkin.p[2];

    // pull out orientation (quaternion)
    qtipcur[0] = tmpkin.q[0];
    qtipcur[1] = tmpkin.q[1];
    qtipcur[2] = tmpkin.q[2];
    qtipcur[3] = tmpkin.q[3];

    // pull out the base angles of the tubes (alpha in rad)	
    alphacur[0] = tmpkin.alpha[0];
    alphacur[1] = tmpkin.alpha[1];
    alphacur[2] = tmpkin.alpha[2];

    // pull out Jacobian
    for(int i = 0; i<6; i++)
    {
        Jcur(0,i)=tmpkin.J1[i];
        Jcur(1,i)=tmpkin.J2[i];
        Jcur(2,i)=tmpkin.J3[i];
        Jcur(3,i)=tmpkin.J4[i];
        Jcur(4,i)=tmpkin.J5[i];
        Jcur(5,i)=tmpkin.J6[i];
    }
}

std_msgs::Bool tmpFKM;
void kinStatusCallback(const std_msgs::Bool &fkmMsg)
{
    tmpFKM = fkmMsg;
    if(tmpFKM.data == true)
    {
        new_kin_msg = 1;
    }
}

geometry_msgs::Pose tempMsg;
void omniCallback(const geometry_msgs::Pose &msg)
{
    tempMsg = msg;
    //    prevOmni = curOmni;

    Eigen::Vector4d qOmni;
    qOmni << tempMsg.orientation.w, tempMsg.orientation.x, tempMsg.orientation.y, tempMsg.orientation.z;
    Eigen::Matrix3d ROmni = quat2rotm(qOmni);

    Eigen::Vector3d pOmni;
    pOmni << tempMsg.position.x, tempMsg.position.y, tempMsg.position.z;

    omniPose.fill(0);
    omniPose.topLeftCorner(3,3) = ROmni;
    omniPose.topRightCorner(3,1) = pOmni;
    omniPose(3,3) = 1.0;

    //    curOmni = omniPose;
}

int buttonState = 0;
int buttonStatePrev = 0;
bool justClutched = false;
void omniButtonCallback(const std_msgs::Int8 &buttonMsg)
{
    buttonStatePrev = buttonState;
    buttonState = static_cast<int>(buttonMsg.data);
    if(buttonState==1 && buttonStatePrev==0)
    {
        justClutched = true;
    }
}

void zero_force()
{
    omniForce.x = 0.0;
    omniForce.y = 0.0;
    omniForce.z = 0.0;
}


int main(int argc, char *argv[])
{
/*******************************************************************************
                INITIALIZE ROS NODE
********************************************************************************/
    ros::init(argc, argv, "resolved_rates");
    ros::NodeHandle node;
/*******************************************************************************
                DECLARATIONS & CONSTANT DEFINITIONS
********************************************************************************/
    // TELEOP PARAMETERS
    double scale_factor = 0.10;
    double lambda_tracking = 10.0; 	// originally 1.0			// TODO: tune these gains
    double lambda_damping = 50.0;    // originally 5.0
    double lambda_jointlim = 100.0;  // originally 10.0

    // motion tracking weighting matrix (task space):
    Matrix6d W_tracking = Eigen::Matrix<double,6,6>::Zero();
    W_tracking(0,0) = lambda_tracking*1.0e6;
    W_tracking(1,1) = lambda_tracking*1.0e6;
    W_tracking(2,2) = lambda_tracking*1.0e6;
    W_tracking(3,3) = 0.1*lambda_tracking*(180.0/M_PI/2.0)*(180.0/M_PI/2.0);
    W_tracking(4,4) = 0.1*lambda_tracking*(180.0/M_PI/2.0)*(180.0/M_PI/2.0);
    W_tracking(5,5) = lambda_tracking*(180.0/M_PI/2.0)*(180.0/M_PI/2.0);

    std::cout << "W_tracking = " << std::endl << W_tracking << std::endl << std::endl;

    // damping weighting matrix (actuator space):
    Matrix6d W_damping = Eigen::Matrix<double,6,6>::Zero();
    double thetadeg = 2.0; // degrees to damp as much as 1 mm  
    W_damping(0,0) = lambda_damping*(180.0/thetadeg/M_PI)*(180.0/thetadeg/M_PI);
    W_damping(1,1) = lambda_damping*(180.0/thetadeg/M_PI)*(180.0/thetadeg/M_PI);
    W_damping(2,2) = lambda_damping*(180.0/thetadeg/M_PI)*(180.0/thetadeg/M_PI);
    W_damping(3,3) = lambda_damping*1.0e6;
    W_damping(4,4) = lambda_damping*1.0e6;
    W_damping(5,5) = lambda_damping*1.0e6;

    std::cout << "W_damping = " << std::endl << W_damping << std::endl << std::endl;

    // conversion from beta to x:
    Eigen::Matrix3d dbeta_dx;
    dbeta_dx << 1, 1, 1,
                0, 1, 1,
                0, 0, 1;

    Eigen::Matrix<double,6,6> dqbeta_dqx;
    dqbeta_dqx.fill(0);
    dqbeta_dqx.block(0,0,3,3) = MatrixXd::Identity(3,3);
    dqbeta_dqx.block(3,3,3,3) = dbeta_dx;

    // ROBOT POSE VARIABLES
    Eigen::Vector3d ptip;
    Eigen::Vector4d qtip;
    Eigen::Matrix3d Rtip;
    Eigen::Vector3d alpha;
    Vector6d q_vec;
    Vector6d delta_qx;
    Matrix4d omniFrameAtClutch;
    Matrix4d robotTipFrameAtClutch; //clutch-in position of cannula
    Matrix4d robotTipFrame; //from tip pose
    Matrix6d J;
    Matrix4d robotDesFrameDelta;
    Vector6d robotDesTwist;
    Eigen::Vector3d L;

    // OMNI POSE VARIABLES
    Matrix4d Tregs;
    Matrix4d omniDelta_omniCoords;
    Matrix4d prevOmniInv;
    Matrix4d omniDelta_cannulaCoords; //change in Omni position between timesteps in inertial frame

    // OMNI REGISTRATION (constant)
    Matrix4d OmniReg = Matrix4d::Identity();
    Eigen::MatrixXd rotationY = Eigen::AngleAxisd(M_PI,Eigen::Vector3d::UnitY()).toRotationMatrix();
    Mtransform::SetRotation(OmniReg,rotationY);

    Matrix4d OmniRegInv = Mtransform::Inverse(OmniReg);

    // MESSAGES TO BE SENT
    endonasal_teleop::config3 q_msg;
    std_msgs::Bool rrUpdateStatusMsg;

    // MISC.
    Eigen::Vector3d zerovec;
    zerovec.fill(0);
    double theta;
    double acosArg;
    double trace;
    Eigen::Matrix3d logR;
    double logRmag;
    Vector6d delta_q;
    Eigen::Matrix<double,6,6> A;
    Eigen::Matrix<double,6,6> Jstar;

/*******************************************************************************
                SET UP PUBLISHERS, SUBSCRIBERS, SERVICES & CLIENTS
********************************************************************************/

    // subscribers
    ros::Subscriber omniButtonSub 	  = node.subscribe("Buttonstates",1,omniButtonCallback);
    ros::Subscriber omniPoseSub   	  = node.subscribe("Omnipos",1,omniCallback);
    ros::Subscriber kinSub 	  	  = node.subscribe("kinematics_output",1,kinCallback);
    ros::Subscriber kinematics_status_pub = node.subscribe("kinematics_status",1,kinStatusCallback);

    // publishers
    ros::Publisher rr_status_pub      = node.advertise<std_msgs::Bool>("rr_status",1000);
    ros::Publisher jointValPub 	      = node.advertise<endonasal_teleop::config3>("joint_q",1000);
    ros::Publisher omniForcePub       = node.advertise<geometry_msgs::Vector3>("Omniforce",1000);
    ros::Publisher pubEncoderCommand1 = node.advertise<medlab_motor_control_board::McbEncoders>("MCB1/encoder_command", 1); // EC13
    ros::Publisher pubEncoderCommand2 = node.advertise<medlab_motor_control_board::McbEncoders>("MCB4/encoder_command", 1); // EC16

    //clients
    ros::ServiceClient startingKinClient = node.serviceClient<endonasal_teleop::getStartingKin>("get_starting_kin");

    // rate
    ros::Rate r(rosLoopRate);

/*******************************************************************************
                LOAD PARAMETERS FROM XML FILES
********************************************************************************/

if(ros::param::has("/CannulaExample1/EndEffectorType"))
{
    std::cout << "I found that parameter you wanted. let's try to do something useful with it" << std::endl;
}

std::string EEtype;
ros::param::get("/CannulaExample1/EndEffectorType",EEtype);

std::cout << "EEtype is now: " << EEtype << std::endl;

//    // based on an example located at: https://gist.github.com/JSchaenzle/2726944

//    std::cout<<"Parsing xml file..."<<std::endl;
//    xml_document<> doc;
//    std::ifstream cannulaFile ("/home/remireaa/catkin_ws/src/endonasal_teleop/config/cannula_example1.xml"); // input file stream (in std library)

    //    // Read xml file into a vector
//    std::vector<char> buffer( (std::istreambuf_iterator<char>(cannulaFile)), std::istreambuf_iterator<char>() );
//    if (buffer.size() > 0)
//    {
//        std::cout << "Successfully loaded xml file." << std::endl;
//    }
//    buffer.push_back('\0');

//    // Parse buffer into doc using the xml file parsing library
//    doc.parse<0>(&buffer[0]);

//    // Process information in cannula xml file
//    xml_node<> *cannula_node = doc.first_node("Cannula");
//    std::cout << "This is an active cannula robot with " << cannula_node->first_attribute("NumberOfTubes")->value() << " tubes." << std::endl;

//    // Iterate over tubes within cannula node:
//    std::cout << "right before loop..." << std::endl;
//    for(xml_node<> *tube_node = cannula_node->first_node("Tube"); tube_node; tube_node = tube_node->next_sibling())
//    {
//        std::cout << "here i am" << std::endl;
//    }

/*******************************************************************************
                COMPUTE KINEMATICS FOR STARTING CONFIGURATION
********************************************************************************/

    Configuration3 qstart;
    qstart.PsiL = Eigen::Vector3d::Zero();
    qstart.Beta << -160.9e-3, -127.2e-3, -86.4e-3;		// TODO: need to set these and get corresponding counts for initialization
    qstart.Ftip = Eigen::Vector3d::Zero();	
    qstart.Ttip = Eigen::Vector3d::Zero();

    q_vec << qstart.PsiL(0), qstart.PsiL(1), qstart.PsiL(2), qstart.Beta(0), qstart.Beta(1), qstart.Beta(2);

    L << 222.5e-3, 163e-3, 104.4e-3;

    // Call getStartingKin service:
    endonasal_teleop::getStartingKin get_starting_kin;
    get_starting_kin.request.kinrequest = true;
    ros::service::waitForService("get_starting_kin",-1);
    zero_force();

    if (startingKinClient.call(get_starting_kin))
    {
        for(int i=0; i<3; i++)
        {
            ptipcur(i) = get_starting_kin.response.p[i];
        }

        for(int i=0; i<4; i++)
        {
            qtipcur(i) = get_starting_kin.response.q[i];
        }

        for (int i=0; i<6; i++)
        {
            Jcur(0,i)=get_starting_kin.response.J1[i];
            Jcur(1,i)=get_starting_kin.response.J2[i];
            Jcur(2,i)=get_starting_kin.response.J3[i];
            Jcur(3,i)=get_starting_kin.response.J4[i];
            Jcur(4,i)=get_starting_kin.response.J5[i];
            Jcur(5,i)=get_starting_kin.response.J6[i];
        }

        new_kin_msg = 1;

        std::cout << "Starting pose and Jacobian received." << std::endl << std::endl;
    }
    else
    {
        std::cout << "Failed to fetch starting pose and Jacobian." << std::endl << std::endl;
        return 1;
    }

    // Check that the kinematics got called once
    std::cout << "ptip at start = " << std::endl << ptipcur << std::endl << std::endl;
    std::cout << "qtip at start = " << std::endl << qtipcur << std::endl << std::endl;
    std::cout << "J at start = " << std::endl << Jcur << std::endl << std::endl;

    Eigen::Vector3d dhPrev;
    dhPrev.fill(0);

    prevOmni = omniPose;

    while (ros::ok())
    {
        if(new_kin_msg==1)
        {
			//new_kin_msg = 0;
            // take a "snapshot" of the current values from the kinematics and Omni for this loop iteration
            curOmni = omniPose;
            ptip = ptipcur;
            qtip = qtipcur;
	    	alpha = alphacur;
            J = Jcur;
            Rtip = quat2rotm(qtip);
            robotTipFrame = assembleTransformation(Rtip,ptip);

		// send commands to motorboards
		double offset_trans_inner = 0; // -46800.0;
		double offset_trans_middle = 0; // -36080.0;
		double offset_trans_outer = 0; // -290.0;
		double scale_rot = 16498.78; 		// counts/rad
		double scale_trans = 6802.16*1e3;		// counts/m
		double scale_trans_outer = 2351.17*1e3;	// counts/m

		medlab_motor_control_board::McbEncoders enc1;
		medlab_motor_control_board::McbEncoders enc2;

//		enc1.count[0] = (int)(q_msg.joint_q[3] * scale_trans - offset_trans_inner); // inner translation
		enc1.count[0] = (int)((q_vec[3] - qstart.Beta[0]) * scale_trans); // inner translation
		enc1.count[1] = (int)(alpha[0] * scale_rot); // inner rotation
		enc1.count[2] = (int)(alpha[2] * scale_rot); // outer rotation
//		enc1.count[3] = (int)(q_msg.joint_q[4] * scale_trans - offset_trans_middle); // middle translation
		enc1.count[3] = (int)((q_vec[4] - qstart.Beta[1]) * scale_trans);  // middle translation
		enc1.count[4] = (int)(alpha[1] * scale_rot); // middle rotation
		enc1.count[5] = 0; // accessory

//		enc2.count[0] = (int)(q_msg.joint_q[5] * scale_trans_outer - offset_trans_outer); // external translation
		enc2.count[0] = 0; // (int)((q_vec[5] - qstart.Beta[2]) * scale_trans_outer); 	// outer translation
		enc2.count[1] = 0;
 		enc2.count[2] = (int)((q_vec[5] - qstart.Beta[2]) * scale_trans_outer); 	// outer translation;
		enc2.count[3] = 0;
 		enc2.count[4] = 0;
  		enc2.count[5] = 0;

		pubEncoderCommand1.publish(enc1); 
		pubEncoderCommand2.publish(enc2);

            if(buttonState==1) //must clutch in button for any motions to happen
            {
                //std::cout << "J(beta) = " << std::endl << J << std::endl << std::endl;
                std::cout << "ptip = " << std::endl << ptip.transpose() << std::endl << std::endl;
                //std::cout << "qtip = " << std::endl << qtip.transpose() << std::endl << std::endl;

				Matrix4d ROmniFrameAtClutch; // Rotation of the Omni frame tip at clutch in

                //furthermore, if this is the first time step of clutch in, we need to save the robot pose & the omni pose
                if(justClutched==true)
                {
                    robotTipFrameAtClutch = robotTipFrame;
                    omniFrameAtClutch = omniPose;
					ROmniFrameAtClutch = assembleTransformation(omniFrameAtClutch.block(0,0,3,3),zerovec);
                    Tregs = assembleTransformation(Rtip.transpose(),zerovec);
                    std::cout << "robotTipFrameAtClutch = " << std::endl << robotTipFrameAtClutch << std::endl << std::endl;
                    std::cout << "omniFrameAtClutch = " << std::endl << omniFrameAtClutch << std::endl << std::endl;
                    justClutched = false; // next time, skip this step
                }

                // find change in omni position and orientation from the clutch pose
                omniDelta_omniCoords = Mtransform::Inverse(ROmniFrameAtClutch.transpose())*Mtransform::Inverse(omniFrameAtClutch)*curOmni*ROmniFrameAtClutch.transpose();
		//omniDelta_omniCoords = Mtransform::Inverse(omniFrameAtClutch)*curOmni;
				// std::cout << "omniDelta_omniCoords = " << std::endl << omniDelta_omniCoords << std::endl << std::endl;

                // expressed in cannula base frame coordinates
                omniDelta_cannulaCoords = OmniRegInv*omniDelta_omniCoords*OmniReg;

                // convert position units mm -> m
                omniDelta_cannulaCoords.block(0,3,3,1) = omniDelta_cannulaCoords.block(0,3,3,1)/1000.0;

                // scale position through scaling ratio
                omniDelta_cannulaCoords.block(0,3,3,1) = scale_factor*omniDelta_cannulaCoords.block(0,3,3,1);

                // scale orientation through scaling ratio (if it is large enough)
                trace = omniDelta_cannulaCoords(0,0)+omniDelta_cannulaCoords(1,1)+omniDelta_cannulaCoords(2,2);
                acosArg = 0.5*(trace-1);
                if(acosArg>1.0) {acosArg = 1.0;}
                if(acosArg<-1.0) {acosArg = -1.0;}
                theta = acos(acosArg);
                if(fabs(theta)>1.0e-3)
                {
                    Eigen::Matrix3d Rdelta;
                    Rdelta = omniDelta_cannulaCoords.block(0,0,3,3);
                    logR = theta/(2*sin(theta))*(Rdelta-Rdelta.transpose());
                    logRmag = logR(2,1)*logR(2,1) + logR(1,0)*logR(1,0) + logR(0,2)*logR(0,2);
                    logRmag *= 0.8;
                    logR *= 0.8;
                    if(logRmag > 1.0e-3)
                    {
                        Rdelta = Eigen::MatrixXd::Identity(3,3) + sin(logRmag)/logRmag*logR + (1-cos(logRmag))/(logRmag*logRmag)*logR*logR;
                        Mtransform::SetRotation(omniDelta_cannulaCoords,Rdelta);
                    }
                }

               // std::cout << "omniDelta_cannulaCoords = " << std::endl << omniDelta_cannulaCoords << std::endl << std::endl;
                //std::cout << "robotTipFrame = " << std::endl << robotTipFrame << std::endl << std::endl;

                // compute the desired robot motion from the omni motion
                robotDesFrameDelta = Mtransform::Inverse(robotTipFrame) * robotTipFrameAtClutch * Tregs * omniDelta_cannulaCoords * Mtransform::Inverse(Tregs);

                // convert to twist coordinates ("wedge" operator)
	 	///*                
		robotDesTwist[0]=robotDesFrameDelta(0,3); //v_x
                robotDesTwist[1]=robotDesFrameDelta(1,3); //v_y
                robotDesTwist[2]=robotDesFrameDelta(2,3); //v_z
                robotDesTwist[3]=robotDesFrameDelta(2,1); //w_x
                robotDesTwist[4]=robotDesFrameDelta(0,2); //w_y
                robotDesTwist[5]=robotDesFrameDelta(1,0); //w_z
		//*/

		/*
		robotDesTwist[0]=0.0; //v_x
                robotDesTwist[1]=0.0; //v_y
                robotDesTwist[2]=0.0; //v_z
                robotDesTwist[3]=0.0; //w_x
                robotDesTwist[4]=0.0; //w_y
                robotDesTwist[5]=0.1; //w_z
		*/

                std::cout << "robotDesTwist = " << std::endl << robotDesTwist.transpose() << std::endl << std::endl;
				robotDesTwist = scaleOmniVelocity(robotDesTwist);
				std::cout << "scaled robotDesTwist = " << std::endl << robotDesTwist.transpose() << std::endl << std::endl;

                // position control only:
//                Eigen::Vector3d delta_x = robotDesTwist.head(3);
//                Eigen::Matrix<double,3,6> Jpos = J.topRows(3);
//                A = (Jpos.transpose()*Jpos + 1e-4*Eigen::MatrixXd::Identity(6,6));
//                Jstar = A.inverse()*Jpos.transpose();

//                // position and orientation control:
//                A = J.transpose()*J + 1e-6*Eigen::MatrixXd::Identity(6,6);
//                Jstar = A.inverse()*J.transpose();

//                //main resolved rates update

//                delta_q = J.colPivHouseholderQr().solve(robotDesTwist);
//                delta_q = Jstar*delta_x;
//                delta_q = Jstar*robotDesTwist;
//                q_vec = q_vec + delta_q;

                // same as what's on the bimanual
                /*Eigen::Matrix<double,6,6> K = J.transpose()*W_tracking*J + W_damping;
                Vector6d V = J.transpose()*W_tracking*robotDesTwist;
                delta_q = K.partialPivLu().solve(V);
				delta_q = saturateJointVelocities(delta_q,rosLoopRate);
                q_vec = q_vec + delta_q;
				*/
				//std::cout << "delta_q = "  << std::endl << delta_q.transpose() << std::endl << std::endl;
				
				// Check if valid betas
				//Vector6d qx_vec = transformBetaToX(q_vec,L);
				//qx_vec.tail(3) = limitBetaValsSimple(qx_vec.tail(3),L);
				//q_vec = transformXToBeta(qx_vec,L);

//                // Transformation from qbeta to qx:
                Eigen::Matrix<double,6,6> Jx = J*dqbeta_dqx;
                //std::cout << "Jx = " << std::endl << Jx << std::endl << std::endl;
                Vector6d qx_vec = transformBetaToX(q_vec,L);

                // Joint limit avoidance weighting matrix
                weightingRet Wout = getWeightingMatrix(qx_vec.tail(3),dhPrev,L,lambda_jointlim);
                Eigen::Matrix<double,6,6> W_jointlim = Wout.W;
                dhPrev = Wout.dh; // and save this dh value for next time

                // Resolved rates update
                Eigen::Matrix<double,6,6> A = Jx.transpose()*W_tracking*Jx + W_damping + W_jointlim;
                Vector6d b = Jx.transpose()*W_tracking*robotDesTwist;
				//Eigen::Matrix<double,6,6> A = Jx.transpose()*W_tracking*Jx;
				//Vector6d b = Jx.transpose()*W_tracking*robotDesTwist;
                delta_qx = A.partialPivLu().solve(b);
				//std::cout <<"delta_qx: "<< delta_qx.transpose() << std::endl << std::endl;

		//delta_qx = saturateJointVelocities(delta_qx, rosLoopRate);
                qx_vec = qx_vec + delta_qx;

                // Correct joint limit violations
                qx_vec.tail(3) = limitBetaValsSimple(qx_vec.tail(3),L);

//                //            Eigen::Matrix<double,6,6> K1 = Jx.transpose()*W_tracking*Jx + W_damping;
//                //            Vector6d V = Jx.transpose()*W_tracking*robotDesTwist;
//                //            Vector6d delta_qx = K1.partialPivLu().solve(V);
//                qx_vec = qx_vec + delta_qx;

//                std::cout << "delta_qx = " << delta_qx << std::endl << std::endl;
//                std::cout << "qx_vec = " << qx_vec << std::endl << std::endl;

                // Transform qbeta back from qx
                q_vec = transformXToBeta(qx_vec,L);
                //std::cout << "q_vec = " << std::endl << q_vec.transpose() << std::endl;
				std::cout << "----------------------------------------------------" << std::endl << std::endl;

                prevOmni = curOmni;

                for(int h = 0; h<6; h++)
                {
                    q_msg.joint_q[h] = q_vec(h);
                    q_msg.joint_q[h+6] = 0;
                }
                rrUpdateStatusMsg.data = true;
            }

            else    // if the button isn't clutched, just send out the current joint values to kinematics
            {
                for(int h = 0; h<6; h++)
                {
                    q_msg.joint_q[h] = q_vec(h);
                    q_msg.joint_q[h+6] = 0;
                }
                rrUpdateStatusMsg.data = true;
            }

            // publish
            jointValPub.publish(q_msg);
            rr_status_pub.publish(rrUpdateStatusMsg);
            omniForcePub.publish(omniForce);


        }

        // sleep
        ros::spinOnce();
        r.sleep();
    }

    return 0;
}

