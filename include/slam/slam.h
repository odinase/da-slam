#ifndef SLAM_H
#define SLAM_H

#include <gtsam/nonlinear/ISAM2.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam_unstable/nonlinear/IncrementalFixedLagSmoother.h>
#include <gtsam/nonlinear/Values.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/geometry/Pose3.h>
#include <vector>

#include "slam/types.h"
#include "jcbb/Hypothesis.h"

namespace slam
{

    using gtsam::symbol_shorthand::L;
    using gtsam::symbol_shorthand::X;

    enum class AssociationMethod
    {
        JCBB,
        ML,
        KnownDataAssociation
    };

    template <class POSE, class POINT>
    class SLAM
    {
    private:
        gtsam::NonlinearFactorGraph graph_;
        gtsam::IncrementalFixedLagSmoother smoother_;

        gtsam::Values estimates_;
        gtsam::Values initial_estimates_;
        gtsam::noiseModel::Diagonal::shared_ptr pose_prior_noise_;
        gtsam::noiseModel::Diagonal::shared_ptr lmk_prior_noise_;

        // gtsam::FastVector<jcbb::Hypothesis> hypotheses_;

        unsigned long int latest_pose_key_;
        POSE latest_pose_;
        unsigned long int latest_landmark_key_;

        void incrementLatestPoseKey() { latest_pose_key_++; }
        void incrementLatestLandmarkKey() { latest_landmark_key_++; }

        void addOdom(const Odometry<POSE> &odom);
        gtsam::FastVector<POINT> predictLandmarks() const;

        double ic_prob_;
        double range_threshold_;

    public:
        SLAM();

        // We need to initialize the graph with priors on the first pose and landmark
        void optimize();
        const gtsam::Values& currentEstimates() const { return estimates_; }
        void processTimestep(const Timestep<POSE, POINT>& timestep);
        void initialize(double ic_prob, const gtsam::Vector &pose_prior_noise, double range_threshold); //, const gtsam::Vector &lmk_prior_noise);
        gtsam::FastVector<POSE> getTrajectory() const;
        gtsam::FastVector<POINT> getLandmarkPoints() const;
        const gtsam::NonlinearFactorGraph& getGraph() const { return smoother_.getFactors(); }
        double error() const { return smoother_.getFactors().error(estimates_); }

        // const gtsam::FastVector<jcbb::Hypothesis> &getChosenHypotheses() const { return hypotheses_; }
        // AssociationMethod getAssociationMethod() const { return association_method_; }
    };

    using SLAM3D = SLAM<gtsam::Pose3, gtsam::Point3>;
    using SLAM2D = SLAM<gtsam::Pose2, gtsam::Point2>;

} // namespace slam

#include "slam/slam.hxx"

#endif // SLAM_H