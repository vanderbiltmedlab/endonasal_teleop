#include "CTR3Robot.h"


CTR3Robot::CTR3Robot()
{
}

//CTR3Robot::CTR3Robot(medlab::CTR3RobotParams params)
//{
//        const CTR::Functions::constant_fun<CTR::Vector<2>::type > k_fun1((1.0 / params.k1)*Eigen::Vector2d::UnitX());
//        const CTR::Functions::constant_fun<CTR::Vector<2>::type > k_fun2((1.0 / params.k2)*Eigen::Vector2d::UnitX());
//        const CTR::Functions::constant_fun<CTR::Vector<2>::type > k_fun3((1.0 / params.k3)*Eigen::Vector2d::UnitX());

//        typedef CTR::Tube<CTR::Functions::constant_fun<CTR::Vector<2>::type> > TubeType;

//        TubeType T1 = CTR::make_annular_tube(params.L1, params.Lt1, params.OD1, params.ID1, k_fun1, params.E, params.G);
//        TubeType T2 = CTR::make_annular_tube(params.L2, params.Lt2, params.OD2, params.ID2, k_fun2, params.E, params.G);
//        TubeType T3 = CTR::make_annular_tube(params.L3, params.Lt3, params.OD3, params.ID3, k_fun3, params.E, params.G);

//        cannula_ = std::make_tuple(T1, T2, T3);
//}

CTR3Robot::~CTR3Robot()
{
	// empty for now
}

void CTR3Robot::init()
{
	// Initial Kinematics Call
	// Forward Kinematics
	qHome_ << 0.0, 0.0, 0.0, -160.9E-3, -127.2E-3, -86.4E-3; //TODO: this needs to be updated for new tubes
	currStateVector_.psiL_ << qHome_.head(3);			// Store everything for current state
	currStateVector_.beta_ << qHome_.tail(3);
	currStateVector_.fTip_ = Eigen::Vector3d::Zero();
	currStateVector_.tTip_ = Eigen::Vector3d::Zero();
        currQVec_ << qHome_;
        currKinematics_ = callKinematicsWithDenseOutput(currQVec_); // interpolation happens in here & sets currInterpolatedBackbone_;
}

bool CTR3Robot::SetCannula(const medlab::CTR3RobotParams params)
{
	const CTR::Functions::constant_fun<CTR::Vector<2>::type > k_fun1((1.0 / params.k1)*Eigen::Vector2d::UnitX()); 
	const CTR::Functions::constant_fun<CTR::Vector<2>::type > k_fun2((1.0 / params.k2)*Eigen::Vector2d::UnitX());
	const CTR::Functions::constant_fun<CTR::Vector<2>::type > k_fun3((1.0 / params.k3)*Eigen::Vector2d::UnitX());

	typedef CTR::Tube<CTR::Functions::constant_fun<CTR::Vector<2>::type> > TubeType;
	
	TubeType T1 = CTR::make_annular_tube(params.L1, params.Lt1, params.OD1, params.ID1, k_fun1, params.E, params.G);
	TubeType T2 = CTR::make_annular_tube(params.L2, params.Lt2, params.OD2, params.ID2, k_fun2, params.E, params.G);
	TubeType T3 = CTR::make_annular_tube(params.L3, params.Lt3, params.OD3, params.ID3, k_fun3, params.E, params.G);

	cannula_ = std::make_tuple(T1, T2, T3);
	
	return true;
}
medlab::Cannula3 CTR3Robot::GetCannula()
{
	return cannula_;
}

bool CTR3Robot::SetCurrStateVector(medlab::CTR3ModelStateVector stateVector)
{
	currStateVector_ = stateVector;
	return true;
}
medlab::CTR3ModelStateVector CTR3Robot::GetCurrStateVector()
{
	return currStateVector_;
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

medlab::KinOut CTR3Robot::callKinematicsWithDenseOutput(RoboticsMath::Vector6d newStateVector)
{
	medlab::CTR3ModelStateVector q;
        q.psiL_ << newStateVector.head<3>;
        q.beta_ << newStateVector.tail<3>;
        q.fTip_ << Eigen::Vector3d::Zero();
        q.tTip_ << Eigen::Vector3d::Zero();

	auto ret1 = CTR::Kinematics_with_dense_output(cannula_, q, medlab::OType());

	int nPts = ret1.arc_length_points.size();
	double* ptr = &ret1.arc_length_points[0];
	Eigen::Map<Eigen::VectorXd> s(ptr, nPts);

	Eigen::MatrixXd poseData = CTR3Robot::forwardKinematics(ret1);

	int nInterp = 200;
	medlab::InterpRet interpResults = interpolateBackbone(s.reverse(), poseData, nInterp);

	Eigen::MatrixXd poseDataOut(8, nInterp + nPts);
	poseDataOut = Eigen::MatrixXd::Zero(8, nInterp + nPts);
	Eigen::RowVectorXd ones(nInterp + nPts);
	ones.fill(1);
	Eigen::VectorXd sOut = interpResults.s;
	poseDataOut.topRows(3) = interpResults.s;
	poseDataOut.middleRows<4>(3) = interpResults.q;
	poseDataOut.bottomRows(1) = ones;


	// TODO: need to parse poseData into KinOut structure
	//currKinematics_ = poseData(...)
	//currKinematics_ = poseDataOut; ??
	//return currKinematics_;
}

Eigen::MatrixXd CTR3Robot::forwardKinematics(auto kin) //TODO: we should not use auto..
{
	// Pick out arc length points
	int nPts = kin.arc_length_points.size();
	double* ptr = &kin.arc_length_points[0];
	Eigen::Map<Eigen::VectorXd> s(ptr, nPts);
	Eigen::VectorXd sAbs(nPts);
	for (int i = 0; i < nPts; i++)
	{
		sAbs(i) = fabs(s(i));
	}

	// Pick out pos and quat for each point expressed in the tip frame
	Eigen::MatrixXd pos(3, nPts);
	Eigen::MatrixXd quat(4, nPts);
	Eigen::MatrixXd psiAngles(3, nPts);
	for (int j = 0; j < nPts; j++)
	{
		double* pPtr = &kin.dense_state_output[j].p[0];
		double* qPtr = &kin.dense_state_output[j].q[o];
		double* psiPtr = &kin.dense_state_output[j].Psi[o];
		Eigen::Map<Eigen::Vector3d> pj(pPtr, 3);
		Eigen::Map<Eigen::Vector4d> qj(qPtr, 4);
		Eigen::Map<Eigen::Vector3d> psij(psiPtr, 3);

		pos.col(j) = pj;
		quat.col(j) = qj;
		psiAngles.col(j) = psij;
	}

	int basePlateIdx;
	sAbs.minCoeff(&basePlateIdx);

	Eigen::Matrix3d Rbt;
	Eigen::Vector3d pbt;
	Eigen::Matrix4d Tbt;
	Eigen::Matrix3d Rjt;
	Eigen::Matrix4d Tjt;
	Eigen::Matrix4d Tjb;
	RoboticsMath::Vector7d tjb;
	RoboticsMath::Vector8d x;

	Eigen::Vector3d baseRotations;
	baseRotations << psiAngles(0, basePlateIdx), psiAngles(1, basePlateIdx), psiAngles(2, basePlateIdx);

	Rbt = RoboticsMath::quat2rotm(quat.col(nPts - 1));
	pbt = pos.col(nPts - 1) - qHome.Beta_(0)*Eigen::Vector3d::UnitZ();  //TODO: why is qhome used here? - I think patrick found this error - we need at s=beta
	Tbt = RoboticsMath::assembleTransformation(Rbt, pos.col(nPts - 1));

	Eigen::MatrixXd poseData(8, nPts);
	for (int j = 0; j < nPts; j++)
	{
		Rjt = RoboticsMath::quat2rotm(quat.col(nPts - j - 1));
		Tjt = RoboticsMath::assembleTransformation(Rjt, pos.col(nPts - j - 1));
		Tjb = RoboticsMath::inverseTransform(Tbt)*Tjt;
		tjb = RoboticsMath::collapseTransform(Tjb);

		x.fill(0);
		x.head<7>() = tjb;
		x(7) = 1.0;
		poseData.col(j) = x;
	}
	return poseData;
}

medlab::InterpRet CTR3Robot::interpolateBackbone(Eigen::VectorXd sRef, Eigen::MatrixXd poseDataRef, const int nPts)
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
	std::sort(xxUnsorted.data(), xxUnsorted.data + xxUnsorted.size());
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
	Eigen::VectorXd::Map(&zVec[0], z.size()) = zVec;
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
