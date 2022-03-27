#include <gtsam/geometry/Pose2.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/nonlinear/GaussNewtonOptimizer.h>
#include <gtsam/sam/BearingRangeFactor.h>
#include <gtsam/slam/dataset.h>
#include <gtsam_unstable/slam/PoseToPointFactor.h>
// #include "slam/utils.h"
#include <cmath>
#include <fstream>
#include <tuple>
#include <algorithm>

#include <glog/logging.h>

// #include "slam/slam_g2o_file.h"
#include "slam/utils_g2o.h"
#include "slam/slam.h"
#include "slam/types.h"
#include "data_association/ml/MaximumLikelihood.h"
#include "visualization/visualization.h"

using gtsam::symbol_shorthand::L; // gtsam/slam/dataset.cpp
using namespace std;
using namespace gtsam;

namespace viz = visualization;

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();
    // default
    string g2oFile = findExampleDataFile("noisyToyGraph.txt");
    bool is3D = false;
    double ic_prob = 0.99;
    std::string output_file;
    double range_threshold = 1e9;
    // Parse user's inputs
    if (argc > 1)
    {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            cout << "Input args: <input dataset filename> <is3D> <ic prob> <range threshold> <output dataset filename>\n";
            return 0;
        }
        g2oFile = argv[1]; // input dataset filename
    }
    if (argc > 2)
    {
        is3D = atoi(argv[2]);
        std::cout << "is3D: " << is3D << std::endl;
    }
    if (argc > 3)
    {
        ic_prob = atof(argv[3]);
        std::cout << "ic_prob: " << ic_prob << std::endl;
    }
    if (argc > 4)
    {
        range_threshold = atof(argv[4]);
        std::cout << "range_threshold: " << range_threshold << std::endl;
    }
    if (argc > 5)
    {
        output_file = argv[5];
        std::cout << "output_file: " << output_file << std::endl;
    }

    vector<boost::shared_ptr<PoseToPointFactor<Pose2, Point2>>> measFactors2d;
    vector<boost::shared_ptr<PoseToPointFactor<Pose3, Point3>>> measFactors3d;

    vector<boost::shared_ptr<BetweenFactor<Pose2>>> odomFactors2d;
    vector<boost::shared_ptr<BetweenFactor<Pose3>>> odomFactors3d;

    // reading file and creating factor graph
    NonlinearFactorGraph::shared_ptr graph;
    Values::shared_ptr initial;
    boost::tie(graph, initial) = readG2owithLmks(g2oFile, is3D, "none");
    auto [odomFactorIdx, measFactorIdx] = findFactors(odomFactors2d, odomFactors3d, measFactors2d, measFactors3d, graph);
#ifdef LOGGING
    double avg_time = 0.0;
#endif
    double total_time = 0.0;
    std::chrono::high_resolution_clock::time_point start_t;
    std::chrono::high_resolution_clock::time_point end_t;
    double final_error;
    Values estimates;
    bool caught_exception = false;

    // Setup visualization
    if (!viz::init())
    {
        cout << "Failed to initialize visualization, aborting!\n";
        return -1;
    }
    else
    {
        cout << "Visualization initialized!\n";
    }

    bool early_stop = false;
    bool next_timestep = true;
    bool enable_stepping = false;
    bool draw_factor_graph = true;

    try
    {
        if (is3D)
        {
            double sigmas = sqrt(da::chi2inv(ic_prob, 3));
            gtsam::Vector pose_prior_noise = (gtsam::Vector(6) << 1e-6, 1e-6, 1e-6, 1e-4, 1e-4, 1e-4).finished();
            pose_prior_noise = pose_prior_noise.array().sqrt().matrix(); // Calc sigmas from variances
            vector<slam::Timestep3D> timesteps = convert_into_timesteps(odomFactors3d, measFactors3d);
            slam::SLAM3D slam_sys{};
            std::shared_ptr<da::DataAssociation<slam::Measurement<gtsam::Point3>>> data_asso = std::make_shared<da::ml::MaximumLikelihood3D>(sigmas, range_threshold);
            slam_sys.initialize(pose_prior_noise, data_asso);
            int tot_timesteps = timesteps.size();
            for (const auto &timestep : timesteps)
            {
                start_t = std::chrono::high_resolution_clock::now();
                slam_sys.processTimestep(timestep);
                end_t = std::chrono::high_resolution_clock::now();
                double duration = chrono::duration_cast<chrono::nanoseconds>(end_t - start_t).count() * 1e-9;
#ifdef LOGGING
                avg_time = (timestep.step * avg_time + duration) / (timestep.step + 1.0);
                cout << "Duration: " << duration << " seconds\n"
                     << "Average time one iteration: " << avg_time << " seconds\n";
#endif
#ifdef HEARTBEAT
                cout << "Processed timestep " << timestep.step << ", " << double(timestep.step + 1) / tot_timesteps * 100.0 << "\% complete\n";
#endif
                total_time += duration;
                final_error = slam_sys.error();
                estimates = slam_sys.currentEstimates();
            }
            NonlinearFactorGraph::shared_ptr graphNoKernel;
            Values::shared_ptr initial2;
            boost::tie(graphNoKernel, initial2) = readG2o(g2oFile, is3D);
            writeG2o(*graphNoKernel, slam_sys.currentEstimates(), output_file);
            ofstream os("/home/odinase/prog/C++/da-slam/graph.txt");
            slam_sys.getGraph().saveGraph(os, slam_sys.currentEstimates());
            os.close();
        }
        else
        {
            double sigmas = sqrt(da::chi2inv(ic_prob, 2));
            gtsam::Vector pose_prior_noise = Vector3(1e-6, 1e-6, 1e-8);
            pose_prior_noise = pose_prior_noise.array().sqrt().matrix(); // Calc sigmas from variances
            vector<slam::Timestep2D> timesteps = convert_into_timesteps(odomFactors2d, measFactors2d);
            slam::SLAM2D slam_sys{};
            std::shared_ptr<da::DataAssociation<slam::Measurement<gtsam::Point2>>> data_asso = std::make_shared<da::ml::MaximumLikelihood2D>(sigmas, range_threshold);
            slam_sys.initialize(pose_prior_noise, data_asso);
            int tot_timesteps = timesteps.size();

            size_t i = 0;
            int step = timesteps[i].step;
            while (viz::running() && i < tot_timesteps)
            {
                viz::new_frame();

                ImGui::Begin("Menu");

                viz::progress_bar(step, tot_timesteps);
                ImGui::Checkbox("Enable stepping", &enable_stepping);
                if (enable_stepping)
                {
                    ImGui::SameLine(0.0f, 100.0f);
                    next_timestep = ImGui::Button("Next timestep");
                }
                else
                {
                    next_timestep = true;
                }
                ImGui::Checkbox("Draw factor graph", &draw_factor_graph);

                ImGui::End(); // Menu

                if (next_timestep)
                {

                    const slam::Timestep2D &timestep = timesteps[i];
                    step = timestep.step;

                    start_t = std::chrono::high_resolution_clock::now();
                    slam_sys.processTimestep(timestep);
                    end_t = std::chrono::high_resolution_clock::now();
                    double duration = chrono::duration_cast<chrono::nanoseconds>(end_t - start_t).count() * 1e-9;
#ifdef LOGGING
                    avg_time = (timestep.step * avg_time + duration) / (timestep.step + 1.0);
                    cout << "Duration: " << duration << " seconds\n"
                         << "Average time one iteration: " << avg_time << " seconds\n";
#endif
#ifdef HEARTBEAT
                    cout << "Processed timestep " << timestep.step << ", " << double(timestep.step + 1) / tot_timesteps * 100.0 << "\% complete\n";
#endif
                    total_time += duration;
                    final_error = slam_sys.error();
                    estimates = slam_sys.currentEstimates();
                    if (enable_stepping)
                    {
                        next_timestep = false;
                    }
                }

                if (draw_factor_graph)
                {
                    ImGui::Begin("Factor graph");
                    if (ImPlot::BeginPlot("##factor graph", ImVec2(-1, -1))) {
                        const auto &graph = slam_sys.getGraph();
                        viz::draw_factor_graph(graph, estimates);
                        ImPlot::EndPlot();
                    }
                    ImGui::End();
                }

                viz::render();
                if (next_timestep)
                {
                    i++;
                    next_timestep = true;
                }
            }
            NonlinearFactorGraph::shared_ptr graphNoKernel;
            Values::shared_ptr initial2;
            boost::tie(graphNoKernel, initial2) = readG2o(g2oFile, is3D);
            writeG2o(*graphNoKernel, slam_sys.currentEstimates(), output_file);
            ofstream os("/home/odinase/prog/C++/da-slam/graph.txt");
            slam_sys.getGraph().saveGraph(os, slam_sys.currentEstimates());
            os.close();
        }
    }
    catch (gtsam::IndeterminantLinearSystemException &indetErr)
    { // when run in terminal: tbb::captured_exception
        std::cout << "Optimization failed" << std::endl;
        std::cout << indetErr.what() << std::endl;
        if (argc > 5)
        {
            string other_msg = "None";
            saveException(output_file, std::string("ExceptionML.txt"), indetErr.what(), other_msg);
        }
        NonlinearFactorGraph::shared_ptr graphNoKernel;
        Values::shared_ptr initial2;
        boost::tie(graphNoKernel, initial2) = readG2o(g2oFile, is3D);
        estimates = *initial2;
        caught_exception = true;
    }
    if (argc < 5)
    {
        if (caught_exception)
        {
            cout << "exception caught! printing odometry" << endl;
        }
        estimates.print("results");
    }
    else
    {
        if (!caught_exception)
        {
            std::cout << "Writing results to file: " << output_file << std::endl;
            NonlinearFactorGraph::shared_ptr graphNoKernel;
            Values::shared_ptr initial2;
            boost::tie(graphNoKernel, initial2) = readG2o(g2oFile, is3D);
            writeG2o(*graphNoKernel, estimates,
                     output_file); // can save pose, ldmk, odom not ldmk measurements
            saveGraphErrors(output_file, std::string("maximum_likelihood"), vector<double>{final_error});
            saveVector(output_file, std::string("errorsGraph.txt"), vector<double>{final_error});
            saveVector(output_file, std::string("runTime.txt"), vector<double>{total_time});
            std::cout << "done! " << std::endl;
        }
    }

    viz::shutdown();
}