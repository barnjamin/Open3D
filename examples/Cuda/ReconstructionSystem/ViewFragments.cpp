//
// Created by wei on 2/5/19.
//

#include <Open3D/Open3D.h>
#include "DatasetConfig.h"

using namespace open3d;
using namespace open3d::io;
using namespace open3d::utility;

int main(int argc, char **argv) {
    SetVerbosityLevel(VerbosityLevel::VerboseDebug);

    DatasetConfig config;

    std::string config_path = argc > 1 ? argv[1] :
        kDefaultDatasetConfigDir + "/stanford/lounge.json";

    bool is_success = ReadIJsonConvertible(config_path, config);
    if (! is_success) return 1;

    auto mesh = CreateMeshFromFile(config.GetReconstructedSceneFile());
    mesh->ComputeTriangleNormals();
    visualization::DrawGeometries({mesh});

    config.GetFragmentFiles();
    for (auto &ply_filename : config.fragment_files_) {
        auto pcl = CreatePointCloudFromFile(ply_filename);
        PrintInfo("%s\n", ply_filename.c_str());
        visualization::DrawGeometries({pcl});
    }
    return 0;
}