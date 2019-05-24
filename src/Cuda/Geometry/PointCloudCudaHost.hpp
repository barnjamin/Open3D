//
// Created by wei on 11/13/18.
//

#pragma once

#include "PointCloudCuda.h"

#include <Cuda/Container/ArrayCuda.h>

namespace open3d {

namespace cuda {
PointCloudCuda::PointCloudCuda()
    : Geometry3D(Geometry::GeometryType::PointCloudCuda) {
    type_ = VertexTypeUnknown;

    max_points_ = -1;
}

PointCloudCuda::PointCloudCuda(
    VertexType type, int max_points)
    : Geometry3D(Geometry::GeometryType::PointCloudCuda) {
    Create(type, max_points);
}

PointCloudCuda::PointCloudCuda(const PointCloudCuda &other)
    : Geometry3D(Geometry::GeometryType::PointCloudCuda) {
    device_ = other.device_;

    points_ = other.points_;

    normals_ = other.normals_;
    colors_ = other.colors_;

    radius_ = other.radius_;
    confidences_ = other.confidences_;
    indices_ = other.indices_;

    type_ = other.type_;
    max_points_ = other.max_points_;
}

PointCloudCuda &PointCloudCuda::operator=(const PointCloudCuda &other) {
    if (this != &other) {
        device_ = other.device_;

        points_ = other.points_;

        normals_ = other.normals_;
        colors_ = other.colors_;

        radius_ = other.radius_;
        confidences_ = other.confidences_;
        indices_ = other.indices_;

        type_ = other.type_;
        max_points_ = other.max_points_;
    }

    return *this;
}

PointCloudCuda::~PointCloudCuda() {
    Release();
}

void PointCloudCuda::Reset() {
    /** No need to clear data **/
    if (type_ == VertexTypeUnknown) {
        utility::PrintError("Unknown vertex type!\n");
    }

    points_.set_iterator(0);

    if (type_ & VertexWithNormal) {
        normals_.set_iterator(0);
    }
    if (type_ & VertexWithColor) {
        colors_.set_iterator(0);
    }
    if (type_ & VertexAsSurfel) {
        radius_.set_iterator(0);
        confidences_.set_iterator(0);
        indices_.set_iterator(0);
    }
}

void PointCloudCuda::Create(VertexType type, int max_points) {
    assert(max_points > 0);
    if (device_ != nullptr) {
        utility::PrintError("[PointCloudCuda] Already created, @Create aborted.\n");
        return;
    }

    if (type == VertexTypeUnknown) {
        utility::PrintError("[PointCloudCuda] Unknown vertex type, @Create aborted!\n");
        return;
    }

    device_ = std::make_shared<PointCloudCudaDevice>();

    type_ = type;
    max_points_ = max_points;

    points_.Create(max_points_);

    if (type_ & VertexWithNormal) {
        normals_.Create(max_points_);
    }
    if (type_ & VertexWithColor) {
        colors_.Create(max_points_);
    }
    if (type_ & VertexAsSurfel) {
        radius_.Create(max_points_);
        confidences_.Create(max_points_);
        indices_.Create(max_points);
    }

    UpdateDevice();
}

void PointCloudCuda::Release() {
    points_.Release();
    normals_.Release();
    colors_.Release();

    radius_.Release();
    confidences_.Release();

    device_ = nullptr;
    type_ = VertexTypeUnknown;
    max_points_ = -1;
}

void PointCloudCuda::UpdateDevice() {
    if (device_ != nullptr) {

        device_->type_ = type_;
        device_->max_points_ = max_points_;

        if (type_ != VertexTypeUnknown) {
            device_->points_ = *points_.device_;
        }

        if (type_ & VertexWithNormal) {
            device_->normals_ = *normals_.device_;
        }
        if (type_ & VertexWithColor) {
            device_->colors_ = *colors_.device_;
        }
        if (type_ & VertexAsSurfel) {
            device_->radius_ = *radius_.device_;
            device_->confidences_ = *confidences_.device_;
            device_->indices_ = *indices_.device_;
        }
    }
}

void PointCloudCuda::Build(RGBDImageCuda &rgbd,
                           PinholeCameraIntrinsicCuda &intrinsic) {
    Reset();
    PointCloudCudaKernelCaller::BuildFromRGBDImage(*this, rgbd, intrinsic);
    if (type_ & VertexWithColor) {
        colors_.set_iterator(points_.size());
    }
}

void PointCloudCuda::Build(ImageCuda<float, 1> &depth,
                           ImageCuda<float, 1> &intensity,
                           PinholeCameraIntrinsicCuda &intrinsic) {
    Reset();
    PointCloudCudaKernelCaller::BuildFromDepthAndIntensityImage(*this, depth, intensity, intrinsic);
}

void PointCloudCuda::Build(ImageCuda<float, 1> &depth,
                           PinholeCameraIntrinsicCuda &intrinsic) {
    Reset();
    PointCloudCudaKernelCaller::BuildFromDepthImage(*this, depth, intrinsic);
}

void PointCloudCuda::Upload(geometry::PointCloud &pcl) {
    if (device_ == nullptr) return;

    std::vector<Vector3f> points, normals, colors;

    if (!pcl.HasPoints()) {
        utility::PrintError("[PointCloudCuda] Empty point cloud, @Upload "
                            "aborted.\n");
        return;
    }

    const size_t N = pcl.points_.size();
    points.resize(N);
    for (int i = 0; i < N; ++i) {
        points[i] = Vector3f(pcl.points_[i](0),
                             pcl.points_[i](1),
                             pcl.points_[i](2));
    }
    points_.Upload(points);

    if ((type_ & VertexWithNormal) && pcl.HasNormals()) {
        normals.resize(N);
        for (int i = 0; i < N; ++i) {
            normals[i] = Vector3f(pcl.normals_[i](0),
                                  pcl.normals_[i](1),
                                  pcl.normals_[i](2));
        }
        normals_.Upload(normals);
    }

    if ((type_ & VertexWithColor) && pcl.HasColors()) {
        colors.resize(N);
        for (int i = 0; i < N; ++i) {
            colors[i] = Vector3f(pcl.colors_[i](0),
                                 pcl.colors_[i](1),
                                 pcl.colors_[i](2));
        }
        colors_.Upload(colors);
    }
}

std::shared_ptr<geometry::PointCloud> PointCloudCuda::Download() {
    std::shared_ptr<geometry::PointCloud> pcl =
        std::make_shared<geometry::PointCloud>();
    if (device_ == nullptr) return pcl;

    if (!HasPoints()) return pcl;

    std::vector<Vector3f> points = points_.Download();

    const size_t N = points.size();
    pcl->points_.resize(N);
    for (int i = 0; i < N; ++i) {
        pcl->points_[i] = Eigen::Vector3d(
            points[i](0), points[i](1), points[i](2));
    }

    if (HasNormals()) {
        std::vector<Vector3f> normals = normals_.Download();
        pcl->normals_.resize(N);
        for (int i = 0; i < N; ++i) {
            pcl->normals_[i] = Eigen::Vector3d(
                normals[i](0), normals[i](1), normals[i](2));
        }
    }

    if (HasColors()) {
        std::vector<Vector3f> colors = colors_.Download();
        pcl->colors_.resize(N);
        for (int i = 0; i < N; ++i) {
            pcl->colors_[i] = Eigen::Vector3d(
                fminf(colors[i](0), 1.0f),
                fminf(colors[i](1), 1.0f),
                fminf(colors[i](2), 1.0f));
        }
    }

    return pcl;
}

bool PointCloudCuda::HasPoints() const {
    if (type_ == VertexTypeUnknown || device_ == nullptr) return false;
    return points_.size() > 0;
}

bool PointCloudCuda::HasNormals() const {
    if ((type_ & VertexWithNormal) == 0 || device_ == nullptr) return false;
    int vertices_size = points_.size();
    return vertices_size > 0 && vertices_size == normals_.size();
}

bool PointCloudCuda::HasColors() const {
    if ((type_ & VertexWithColor) == 0 || device_ == nullptr) return false;
    int vertices_size = points_.size();
    return vertices_size > 0 && vertices_size == colors_.size();
}

void PointCloudCuda::Clear() {
    Reset();
}

bool PointCloudCuda::IsEmpty() const {
    return !HasPoints();
}

Eigen::Vector3d PointCloudCuda::GetMinBound() const {
    if (device_ == nullptr) return Eigen::Vector3d(0, 0, 0);

    const int num_vertices = points_.size();
    if (num_vertices == 0) return Eigen::Vector3d(0, 0, 0);

    ArrayCuda<Vector3f> min_bound_cuda(1);
    std::vector<Vector3f> min_bound = {Vector3f(1e10f, 1e10f, 1e10f)};
    min_bound_cuda.Upload(min_bound);

    PointCloudCudaKernelCaller::GetMinBound(*this, min_bound_cuda);

    min_bound = min_bound_cuda.Download();
    return min_bound[0].ToEigen();
}

Eigen::Vector3d PointCloudCuda::GetMaxBound() const {
    if (device_ == nullptr) return Eigen::Vector3d(10, 10, 10);

    const int num_vertices = points_.size();
    if (num_vertices == 0) return Eigen::Vector3d(0, 0, 0);

    ArrayCuda<Vector3f> max_bound_cuda(1);
    std::vector<Vector3f> max_bound = {Vector3f(-1e10f, -1e10f, -1e10f)};
    max_bound_cuda.Upload(max_bound);

    PointCloudCudaKernelCaller::GetMaxBound(*this, max_bound_cuda);

    max_bound = max_bound_cuda.Download();
    return max_bound[0].ToEigen();
}

PointCloudCuda &PointCloudCuda::Transform(const Eigen::Matrix4d &transformation) {
    if (device_ == nullptr) return *this;

    const int num_vertices = points_.size();
    if (num_vertices == 0) return *this;

    TransformCuda transformation_cuda;
    transformation_cuda.FromEigen(transformation);

    PointCloudCudaKernelCaller::Transform(
        *this, transformation_cuda);

    return *this;
}


PointCloudCuda& PointCloudCuda::Translate(const Eigen::Vector3d& translation) {
    return *this;
}

PointCloudCuda& PointCloudCuda::Scale(const double scale) {
    return *this;
}

PointCloudCuda& PointCloudCuda::Rotate(const Eigen::Vector3d& rotation,
                       open3d::geometry::Geometry3D::RotationType type) {
    return *this;
}

std::tuple<Eigen::Vector3d, double> PointCloudCuda::Normalize() {
    Eigen::Vector3d mean = ComputeMean();
    double scale = SubMeanAndGetMaxScale(mean);
    return std::make_tuple(mean, scale);
}


void PointCloudCuda::Rescale(double scale) {
    PointCloudCudaKernelCaller::Rescale(*this, (float) scale);
}

Eigen::Vector3d PointCloudCuda::ComputeMean() {
    ArrayCuda<Vector3f> sum;
    sum.Resize(1);
    sum.Memset(0);

    PointCloudCudaKernelCaller::ComputeSum(*this, sum);
    auto downloaded_sum = sum.DownloadAll();

    return downloaded_sum[0].ToEigen() / points_.size();
}

double PointCloudCuda::SubMeanAndGetMaxScale(Eigen::Vector3d &mean) {
    Vector3f mean_cuda;
    mean_cuda.FromEigen(mean);

    ArrayCuda<float> scale;
    scale.Resize(1);
    scale.Memset(0);

    PointCloudCudaKernelCaller::Normalize(*this, mean_cuda, scale);
    auto downloaded_scale = scale.DownloadAll();

    return double(downloaded_scale[0]);
}

} // cuda
} // open3d
