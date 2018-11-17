#pragma once
#include "CTR3Robot.h"
#include "RoboticsMath.h"
#include <vector>

class ResolvedRatesController {

public:

  enum class LIMIT_FLAG
  {
    VELOCITY_T1_ROT, VELOCITY_T1_TRANS,
    VELOCITY_T2_ROT, VELOCITY_T2_TRANS,
    VELOCITY_T3_ROT, VELOCITY_T3_TRANS,
    T1_BACK, T1_FRONT,
    T2_BACK, T2_FRONT,
    T3_BACK, T3_FRONT //TODO: implement these
  };

  ResolvedRatesController(medlab::Cannula3 cannula);  // setup
  ~ResolvedRatesController();
  void init();		// go online
 /* RoboticsMath::Matrix6d step(SomeData::Type desiredTwist);*/ // output of this should be joint positions [alpha, beta]
  RoboticsMath::Matrix6d step();
  bool SetTrackingGain(double LambdaTracking);
  bool SetDampingGain(double LambdaDamping);
  bool SetJointLimitsGain(double LambdaJL);
  bool SetCurInputDevice();
  bool SetCurCTR3Robot();
  //bool SetInputDeviceTransform(Matrix4d TRegistration); //TODO: all preprocessing of input device twist should be done outside of this class

  std::vector<ResolvedRatesController::LIMIT_FLAG> GetLimitFlags(){return currentLimitFlags_;} //TODO: implement this

private:
  CTR3Robot robot_;
  std::vector<ResolvedRatesController::LIMIT_FLAG> currentLimitFlags_;
  RoboticsMath::Matrix6d JCur_;
  RoboticsMath::Matrix6d WTracking_;
  RoboticsMath::Matrix6d WDamping_;
  RoboticsMath::Matrix6d WJointLims_;
  medlab::InterpRet InterpolatedBackboneCur_;
  double rosLoopRate_;
  // TODO: add pointer to current CTR3Robot this is in charge of

  RoboticsMath::Vector6d saturateJointVelocities(RoboticsMath::Vector6d delta_qx, int node_freq);
  RoboticsMath::Vector6d transformBetaToX(RoboticsMath::Vector6d qbeta, Eigen::Vector3d L);
  RoboticsMath::Vector6d transformXToBeta(RoboticsMath::Vector6d qx, Eigen::Vector3d L);
  double dhFunction(double xmin, double xmax, double x);
  medlab::WeightingRet computeWeightingMatrix(Eigen::Vector3d x, Eigen::Vector3d dhPrev, Eigen::Vector3d L, double lambda);
  Eigen::Vector3d limitBetaValsSimple(Eigen::Vector3d x_in, Eigen::Vector3d L);
  Eigen::Vector3d limitBetaValsBimanualAlgorithm(Eigen::Vector3d Beta_in, Eigen::Vector3d L_in);
  RoboticsMath::Vector6d scaleInputDeviceVelocity(RoboticsMath::Vector6d desTwistDelta);

};
