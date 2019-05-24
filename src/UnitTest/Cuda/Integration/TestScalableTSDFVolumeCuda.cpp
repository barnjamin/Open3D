//
// Created by wei on 10/20/18.
//

#include <Open3D/Open3D.h>
#include <Cuda/Integration/ScalableTSDFVolumeCuda.h>
#include <Cuda/Geometry/ImageCuda.h>
#include <opencv2/opencv.hpp>
#include <gtest/gtest.h>

using namespace open3d;
using namespace open3d::cuda;
using namespace open3d::io;
using namespace open3d::geometry;
using namespace open3d::utility;

TEST(ScalableTSDFVolumeCuda, Create) {
    ScalableTSDFVolumeCuda volume;
    volume.Create(8, 10000, 200000);

    ScalableTSDFVolumeCuda volume_copy;
    volume_copy = volume;
}

TEST(ScalableTSDFVolumeCuda, TouchSubvolumes) {
    using namespace open3d;

    Image depth, color;
    ReadImage("../../../examples/TestData/RGBD/depth/00000.png", depth);
    ReadImage("../../../examples/TestData/RGBD/color/00000.jpg", color);

    RGBDImageCuda rgbd(640, 480, 3.0, 1000.0f);
    rgbd.Upload(depth, color);

    PinholeCameraIntrinsicCuda intrinsics(
        camera::PinholeCameraIntrinsicParameters::PrimeSenseDefault);

    float voxel_length = 0.01f;
    TransformCuda extrinsics = TransformCuda::Identity();
    extrinsics(0, 3) = 10.0f;
    extrinsics(1, 3) = -10.0f;
    extrinsics(2, 3) = 1.0f;
    ScalableTSDFVolumeCuda volume(8, voxel_length, 3 * voxel_length,
                                  extrinsics,
                                  10000, 200000);

    volume.TouchSubvolumes(rgbd.depth_, intrinsics, extrinsics);
    volume.GetSubvolumesInFrustum(intrinsics, extrinsics);

    auto entry_vector = volume.active_subvolume_entry_array_.Download();
    for (auto &entry : entry_vector) {
        PrintInfo("%d %d %d %d\n", entry.key(0), entry.key(1), entry.key(2),
            entry.internal_addr);
    }
}

TEST(ScalableTSDFVolumeCuda, Integration) {
    using namespace open3d;
    Image depth, color;
    ReadImage("../../../examples/TestData/RGBD/depth/00000.png", depth);
    ReadImage("../../../examples/TestData/RGBD/color/00000.jpg", color);

    RGBDImageCuda rgbd(640, 480, 3.0, 1000.0f);
    rgbd.Upload(depth, color);

    PinholeCameraIntrinsicCuda intrinsics(
        camera::PinholeCameraIntrinsicParameters::PrimeSenseDefault);

    float voxel_length = 0.01f;
    TransformCuda extrinsics = TransformCuda::Identity();
    ScalableTSDFVolumeCuda volume(8, voxel_length, 3 * voxel_length,
                                  extrinsics,
                                  10000, 200000);
    Timer timer;
    timer.Start();
    for (int i = 0; i < 10; ++i) {
        volume.Integrate(rgbd, intrinsics, extrinsics);
    }
    timer.Stop();
    PrintInfo("Integrations takes %f milliseconds\n", timer.GetDuration() / 10);

    PrintInfo("Downloading volumes: \n");
    auto result = volume.DownloadVolumes();
    auto &keys = result.first;
    auto &volumes = result.second;
    for (int i = 0; i < keys.size(); ++i) {
        float sum_tsdf = 0;
        auto &volume = volumes[i];
        auto &tsdf = volume.tsdf_;
        for (int k = 0; k < 512; ++k) {
            sum_tsdf += fabsf(tsdf[k]);
        }
        PrintInfo("%d %d %d %f\n", keys[i](0), keys[i](1), keys[i](2), sum_tsdf);
    }
}

TEST(ScalableTSDFVolumeCuda, RayCasting) {
    using namespace open3d;
    Image depth, color;
    ReadImage("../../../examples/TestData/RGBD/depth/00000.png", depth);
    ReadImage("../../../examples/TestData/RGBD/color/00000.jpg", color);

    RGBDImageCuda rgbd(640, 480, 3.0, 1000.0f);
    rgbd.Upload(depth, color);

    PinholeCameraIntrinsicCuda intrinsics(
        camera::PinholeCameraIntrinsicParameters::PrimeSenseDefault);

    float voxel_length = 0.01f;
    TransformCuda extrinsics = TransformCuda::Identity();
    extrinsics(0, 3) = 10.0f;
    extrinsics(1, 3) = -10.0f;
    extrinsics(2, 3) = 1.0f;

    ScalableTSDFVolumeCuda volume(8, voxel_length, 3 * voxel_length,
                                  extrinsics,
                                  10000, 200000);

    ImageCuda<float, 3> raycaster(depth.width_, depth.height_);

    Timer timer;
    const int iters = 10;
    float time = 0;
    for (int i = 0; i < iters; ++i) {
        volume.Integrate(rgbd, intrinsics, extrinsics);
        timer.Start();
        volume.RayCasting(raycaster, intrinsics, extrinsics);
        timer.Stop();
        time += timer.GetDuration();

        cv::imshow("Raycaster", raycaster.DownloadMat());
        cv::waitKey(10);
    }
    cv::waitKey(-1);

    cv::Mat ray_casted = raycaster.DownloadMat();
    cv::Mat read = cv::imread("../../../examples/TestData/RGBD/depth/00000.png", cv::IMREAD_UNCHANGED);
    for (int i = 0; i < ray_casted.rows; ++i) {
        for (int j = 0; j < ray_casted.cols; ++j) {
            std::cout << ray_casted.at<cv::Vec3f>(i, j)[2] * 1000 << " "
                      << read.at<ushort>(i, j) << std::endl;
        }
    }

    PrintInfo("Raycasting takes %f milliseconds\n", time / iters);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}