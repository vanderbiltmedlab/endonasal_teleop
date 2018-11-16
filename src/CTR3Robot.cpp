#include "CTR3Robot.h"
#include <vector>
#include <tuple>
#include "RoboticsMath.h"
#include "MedlabTypes.h"
#include <Kinematics.h>

CTR3Robot::CTR3Robot(medlab::Cannula3 cannula):
  cannula_(cannula)
{

}

void CTR3Robot::init()
{

  qHome_ << 0.0, 0.0, 0.0, -160.9E-3, -127.2E-3, -86.4E-3; //TODO: this needs to be updated for new tubes
  currKinematicsInput_.PsiL = qHome_.head(3);			// Store everything for current state
  currKinematicsInput_.Beta = qHome_.tail(3);
  currKinematicsInput_.Ftip = Eigen::Vector3d::Zero();
  currKinematicsInput_.Ttip = Eigen::Vector3d::Zero();
  currQVec_ << qHome_;
  currKinematics_ = callKinematicsWithDenseOutput(currKinematicsInput_); // interpolation happens in here & sets currInterpolatedBackbone_;
}

medlab::Cannula3 CTR3Robot::GetCannula()
{
  return cannula_;
}

bool CTR3Robot::SetCurrKinematicsInput(medlab::CTR3KinematicsInputVector kinematicsInput)
{
  currKinematicsInput_ = kinematicsInput;
  return true;
}
medlab::CTR3KinematicsInputVector CTR3Robot::GetCurrKinematicsInput()
{
  return currKinematicsInput_;
}

bool CTR3Robot::SetCurrQVec(RoboticsMath::Vector6d qVec)
{
  currQVec_ = qVec;
  return true;
}
RoboticsMath::Vector6d CTR3Robot::GetCurrQVec()
{
  return currQVec_;
}

bool CTR3Robot::SetInterpolatedBackbone(medlab::InterpRet interpolatedBackbone)
{
  currInterpolatedBackbone_ = interpolatedBackbone;
  return true;
}
medlab::InterpRet CTR3Robot::GetInterpolatedBackbone()
{
  return currInterpolatedBackbone_;
}

medlab::KinOut CTR3Robot::callKinematicsWithDenseOutput(medlab::CTR3KinematicsInputVector newKinematicsInput)
{

  medlab::KinOut kinoutput;
  auto ret1 = CTR::Kinematics_with_dense_output(cannula_, newKinematicsInput, medlab::OType());

  RoboticsMath::Matrix6d J;
  J = CTR::GetTipJacobianForTube1(ret1.y_final);

  double Stability;
  Stability = CTR::GetStability(ret1.y_final);

  int nPts = ret1.arc_length_points.size();
  double* ptr = &ret1.arc_length_points[0];
  Eigen::Map<Eigen::VectorXd> s(ptr, nPts);

  Eigen::MatrixXd poseData = CTR3Robot::forwardKinematics(ret1);

  int nInterp = 200;
  medlab::InterpRet interpResults = interpolateBackbone(s, poseData, nInterp);

  Eigen::MatrixXd poseDataOut(8, nInterp + nPts);
  poseDataOut = Eigen::MatrixXd::Zero(8, nInterp + nPts);
  Eigen::RowVectorXd ones(nInterp + nPts);
  ones.fill(1);
  Eigen::VectorXd sOut = interpResults.s;
  poseDataOut.topRows(3) = interpResults.p;
  poseDataOut.middleRows<4>(3) = interpResults.q;
  poseDataOut.bottomRows(1) = ones;

  Eigen::Vector3d pTip;
  Eigen::Vector4d qBishop;
  Eigen::Matrix3d RBishop;
  Eigen::Matrix3d Rtip;
  Eigen::Vector4d qTip;


  pTip = ret1.pTip;
  qBishop = ret1.qTip;
  RBishop = RoboticsMath::quat2rotm(qBishop);
  Eigen::Matrix3d rotate_psiL = Eigen::Matrix3d::Identity();
  rotate_psiL(0,0) = cos(newKinematicsInput.PsiL(0));
  rotate_psiL(0,1) = -sin(newKinematicsInput.PsiL(0));
  rotate_psiL(1,0) = sin(newKinematicsInput.PsiL(0));
  rotate_psiL(1,1) = cos(newKinematicsInput.PsiL(0));
  Rtip = RBishop*rotate_psiL;
  qTip = RoboticsMath::rotm2quat(Rtip);

  // Parse kinret into currKinematics_
  kinoutput.Ptip[0] = pTip[0];
  kinoutput.Ptip[1] = pTip[1];
  kinoutput.Ptip[2] = pTip[2];
  kinoutput.Qtip[0] = qTip[0];
  kinoutput.Qtip[1] = qTip[1];
  kinoutput.Qtip[2] = qTip[2];
  kinoutput.Qtip[3] = qTip[3];
  kinoutput.Qbishop[0] = qBishop[0];
  kinoutput.Qbishop[1] = qBishop[1];
  kinoutput.Qbishop[2] = qBishop[2];
  kinoutput.Qbishop[3] = qBishop[3];
  kinoutput.Alpha[0] = ret1.y_final.Psi[0];
  kinoutput.Alpha[1] = ret1.y_final.Psi[1];
  kinoutput.Alpha[2] = ret1.y_final.Psi[2];
  kinoutput.Stability = Stability;
  kinoutput.Jtip = J;

  currKinematics_ = kinoutput;

  return kinoutput;
}

Eigen::MatrixXd CTR3Robot::forwardKinematics(auto kin) //TODO: we should not use auto..?
{

  // Pick out arc length points
  int nPts = kin.arc_length_points.size();
  double* ptr = &kin.arc_length_points[0];
  Eigen::Map<Eigen::VectorXd> s(ptr, nPts);

  double zeroIndex;
  Eigen::VectorXd zeroIndexVec;
  Eigen::Vector3d pZero;
  Eigen::Vector4d qZero;
  Eigen::Matrix4d gZero;
  Eigen::Matrix4d gStarZero;
  Eigen::Matrix4d gStarL;

  int count = 0;
  for (int i=0; i < s.size(); ++i)
  {
    if (s(i) == 0) {
      zeroIndexVec.resize(count+1);
      zeroIndexVec(count) = (double) i;
      count ++;
    }
  }

  zeroIndex = zeroIndexVec(count-1);
  pZero = kin.dense_state_output.at( zeroIndex ).p;
  qZero = kin.dense_state_output.at( zeroIndex ).q;
  gZero = RoboticsMath::assembleTransformation(RoboticsMath::quat2rotm(qZero),pZero);
  gStarZero = RoboticsMath::assembleTransformation(Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero());
  gStarL = gStarZero*RoboticsMath::inverseTransform(gZero);

  Eigen::MatrixXd poseData(8,nPts);
  for (int i =0; i < nPts; ++i)
  {
    Eigen::Vector3d pi = kin.dense_state_output.at( i ).p;
    Eigen::Vector4d qi = kin.dense_state_output.at( i ).q;
    Eigen::Matrix4d gi = RoboticsMath::assembleTransformation(RoboticsMath::quat2rotm(qi),pi);
    Eigen::Matrix4d gStari = gStarL*gi;
    RoboticsMath::Vector8d xi;
    xi.fill(0);
    xi.head<7>() = RoboticsMath::collapseTransform(gStari);
    xi(7) = 1.0;
    poseData.col(i) = xi;
  }

  return poseData;
}

medlab::InterpRet CTR3Robot::interpolateBackbone(Eigen::VectorXd sRef, Eigen::MatrixXd poseDataRef, int nPts)
{
  Eigen::Matrix<double, 4, Eigen::Dynamic> qRef;
  qRef = poseDataRef.middleRows<4>(3);

  // Create a zero to one list for ref arc lengths
  int nRef = sRef.size();
  double totalArcLength = sRef(nRef - 1) - sRef(0);
  Eigen::VectorXd sRef0Vec(nRef);
  sRef0Vec.fill(sRef(0));
  Eigen::VectorXd zeroToOne = (1 / totalArcLength)*(sRef - sRef0Vec);

  // Create a zero to one vector including ref arc lengths & interp arc lengths (evenly spaced)
  int nPtsTotal = nPts + nRef;
  lastPosInterp_ = nPtsTotal - 1;

  Eigen::VectorXd xxLinspace(nPts);
  xxLinspace.fill(0.0);
  xxLinspace.setLinSpaced(nPts, 0.0, 1.0);
  Eigen::VectorXd xxUnsorted(nPtsTotal);
  xxUnsorted << xxLinspace, zeroToOne;
  std::sort(xxUnsorted.data(), xxUnsorted.data() + xxUnsorted.size());
  Eigen::VectorXd xx = xxUnsorted.reverse(); //Rich's interpolation functions call for descending order

  // List of return arc lengths in the original scaling/offset
  Eigen::VectorXd xxSRef0Vec(nPtsTotal);
  xxSRef0Vec.fill(sRef(0));
  Eigen::VectorXd sInterp = totalArcLength*xx.reverse() + xxSRef0Vec;

  // Interpolate to find list of return quaternions
  Eigen::MatrixXd qInterp1 = RoboticsMath::quatInterp(qRef.rowwise().reverse(), zeroToOne.reverse(), xx);
  Eigen::MatrixXd qInterp = qInterp1.rowwise().reverse();

  // Interpolate to find list of return positions
  std::vector<double> sVec;
  sVec.resize(sRef.size());
  Eigen::VectorXd::Map(&sVec[0], sRef.size()) = sRef;

  Eigen::VectorXd x = poseDataRef.row(0); // interp x
  std::vector<double> xVec;
  xVec.resize(x.size());
  Eigen::VectorXd::Map(&xVec[0], x.size()) = x;
  tk::spline Sx;
  Sx.set_points(sVec, xVec);
  Eigen::VectorXd xInterp(nPtsTotal);
  xInterp.fill(0);
  for (int i = 0; i < nPtsTotal; i++)
  {
    xInterp(i) = Sx(sInterp(i));
  }

  Eigen::VectorXd y = poseDataRef.row(1); // interp y
  std::vector<double> yVec;
  yVec.resize(y.size());
  Eigen::VectorXd::Map(&yVec[0], y.size()) = y;
  tk::spline Sy;
  Sy.set_points(sVec, yVec);
  Eigen::VectorXd yInterp(nPtsTotal);
  yInterp.fill(0);
  for (int i = 0; i < nPtsTotal; i++)
  {
    yInterp(i) = Sy(sInterp(i));
  }

  Eigen::VectorXd z = poseDataRef.row(2); // interp z
  std::vector<double> zVec;
  zVec.resize(z.size());
  Eigen::VectorXd::Map(&zVec[0], z.size()) = z;
  tk::spline Sz;
  Sz.set_points(sVec, zVec);
  Eigen::VectorXd zInterp(nPtsTotal);
  for (int i = 0; i < nPtsTotal; i++)
  {
    zInterp(i) = Sz(sInterp(i));
  }

  Eigen::MatrixXd pInterp(3, nPtsTotal);
  pInterp.fill(0);
  pInterp.row(0) = xInterp.transpose();
  pInterp.row(1) = yInterp.transpose();
  pInterp.row(2) = zInterp.transpose();

  medlab::InterpRet interpResults;
  interpResults.s = sInterp;
  interpResults.p = pInterp;
  interpResults.q = qInterp;

  currInterpolatedBackbone_ = interpResults;

  return interpResults;

}
