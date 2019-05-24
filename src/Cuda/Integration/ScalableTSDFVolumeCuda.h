//
// Created by wei on 10/10/18.
//

#pragma once

#include "IntegrationClasses.h"
#include <Cuda/Common/UtilsCuda.h>

#include <Cuda/Container/HashTableCuda.h>

#include <Cuda/Camera/PinholeCameraIntrinsicCuda.h>
#include <Cuda/Common/TransformCuda.h>
#include <Cuda/Common/LinearAlgebraCuda.h>
#include <Cuda/Geometry/RGBDImageCuda.h>

#include <memory>
#include <Cuda/Geometry/PointCloudCuda.h>

namespace open3d {
namespace cuda {

class ScalableTSDFVolumeCudaDevice {
public:
    typedef HashTableCudaDevice<
    Vector3i, UniformTSDFVolumeCudaDevice, SpatialHasher>
        SpatialHashTableCudaDevice;

public:
    int N_;

    /** (N * N * N) * value_capacity **/
    float *tsdf_memory_pool_;
    uchar *weight_memory_pool_;
    Vector3b *color_memory_pool_;

    /** These are return values when subvolume is null.
     *  Refer to tsdf(), etc **/
    float tsdf_dummy_ = 0;
    uchar weight_dummy_ = 0;
    Vector3b color_dummy_ = Vector3b(0);

    SpatialHashTableCudaDevice hash_table_;

    /** Below are the extended part of the hash table.
     *  The essential idea is that we wish to
     *  1. Compress all the hashed subvolumes so that they can be processed in
     *     parallel with trivial indexing. (Active subvolumes)
     *  2. Meanwhile know its reverse, i.e. what's the index in array of the
     *     active subvolumes given the 3D index.
     *  Refer to function @ActivateSubvolume **/
    /** f: R -> R^3. Given index, query subvolume coordinate **/
    ArrayCudaDevice<HashEntry<Vector3i>> active_subvolume_entry_array_;
    /** f: R^3 -> R -> R. Given subvolume coordinate, query index **/
    int *active_subvolume_indices_;

public:
    int bucket_count_;
    int value_capacity_;

    float voxel_length_;
    float inv_voxel_length_;
    float sdf_trunc_;
    TransformCuda transform_volume_to_world_;
    TransformCuda transform_world_to_volume_;

public:
    /** This adds the entry into the active entry array,
     *  waiting for parallel processing.
     *  The tricky part is that we also have to know its reverse (for meshing):
     *  given Xsv, we also wish to know the active subvolume id.
     *  That is stored in @active_subvolume_indices_. **/
    __DEVICE__ void ActivateSubvolume(const HashEntry<Vector3i> &entry);
    __DEVICE__ int QueryActiveSubvolumeIndex(const Vector3i &key);

public:
    /** Coordinate conversions
     *  Duplicate functions of UniformTSDFVolume (how to simplify?) **/
    __DEVICE__ inline Vector3f voxelf_to_world(const Vector3f &X);
    __DEVICE__ inline Vector3f world_to_voxelf(const Vector3f &Xw);
    __DEVICE__ inline Vector3f voxelf_to_volume(const Vector3f &X);
    __DEVICE__ inline Vector3f volume_to_voxelf(const Vector3f &Xv);

    /** Similar to LocateVolumeUnit **/
    __DEVICE__ inline Vector3i voxel_locate_subvolume(const Vector3i &X);
    __DEVICE__ inline Vector3i voxelf_locate_subvolume(const Vector3f &X);

    __DEVICE__ inline Vector3i voxel_global_to_local(
        const Vector3i &X, const Vector3i &Xsv);
    __DEVICE__ inline Vector3f voxelf_global_to_local(
        const Vector3f &X, const Vector3i &Xsv);

    __DEVICE__ inline Vector3i voxel_local_to_global(
        const Vector3i &Xlocal, const Vector3i &Xsv);
    __DEVICE__ inline Vector3f voxelf_local_to_global(
        const Vector3f &Xlocal, const Vector3i &Xsv);

    __DEVICE__ UniformTSDFVolumeCudaDevice *QuerySubvolume(
        const Vector3i &Xsv);

    /** Unoptimized access and interpolation
     * (required hash-table access every access, good for RayCasting) **/
    __DEVICE__ float &tsdf(const Vector3i &X);
    __DEVICE__ uchar &weight(const Vector3i &X);
    __DEVICE__ Vector3b &color(const Vector3i &X);

    __DEVICE__ float TSDFAt(const Vector3f &X);
    __DEVICE__ uchar WeightAt(const Vector3f &X);
    __DEVICE__ Vector3b ColorAt(const Vector3f &X);
    __DEVICE__ Vector3f GradientAt(const Vector3f &X);

    /**
     * Optimized access and interpolation using cached neighbor subvolumes.
     *
     * NOTE:
     * Interpolation, especially on boundaries, is very expensive when kernels
     * frequently query neighbor volumes.
     * In a typical kernel like MC, we cache them in __shared__ memory.
     *
     * In the beginning of a kernel, we query these volumes and syncthreads.
     * The neighbors are stored in __shared__ subvolumes[N].
     *
     * > The neighbor subvolume indices for value interpolation are
     *  (0, 1) x (0, 1) x (0, 1) -> N = 8
     *
     * > For gradient interpolation, more neighbors have to be considered
     *   (0, 1) x (0, 1) x (0, 1)   8
     *   (-1, ) x (0, 1) x (0, 1) + 4
     *   (0, 1) x (-1, ) x (0, 1) + 4
     *   (0, 1) x (0, 1) x (-1, ) + 4 -> N = 20
     *
     * > To simplify, we define all the 27 neighbor indices
     *   (not necessary to assign them all in pre-processing, but it is
     *   anyway not too expensive to do so).
     *   (-1, 0, 1) ^ 3 -> N = 27
     *   The 3D neighbor indices are converted to 1D in LinearizeNeighborIndex.
     *
     * ---
     *
     * Given X = (x, y, z) in voxel units, the optimized query should be:
     * 0. > Decide the subvolume this kernel is working on (Xsv, can be
     *      stored in @target_subvolume_entry_array_ beforehand),
     *      and pre-allocate to cache neighbor @cached_subvolumes
     *      in shared memory.
     *
     *      For each voxel (X):
     * 1. > Get voxel coordinate in this specific volume
     *      Vector3f Xlocal = voxel[f]_global_to_local(X, Xsv);
     *
     * 2. > Check if it is on boundary (danger zone)
     *      OnBoundary[f](Xlocal)
     *
     * 3. > If not, directly use subvolumes[LinearizeNeighborOffset(Vector3i(0)]
     *      to access/interpolate data
     *
     * 4. > If so, use XXXOnBorderAt(Xlocal) to interpolate.
     *      These functions will first query neighbors for each point to
     *      interpolate, and access them from cached subvolumes
     *        Vector3i dXsv = NeighborOffsetOfBoundaryVoxel(Xlocal)
     *        cached_subvolumes[LinearizeNeighborOffset(dXsv)].tsdf() ...
     */
    /** Decide which version of function we should use **/
    __DEVICE__ inline bool OnBoundary(
        const Vector3i &Xlocal, bool for_gradient = false);
    __DEVICE__ inline bool OnBoundaryf(
        const Vector3f &Xlocal, bool for_gradient = false);

    /** Query neighbor subvolumes and cache them or access them **/
    __DEVICE__ inline Vector3i NeighborOffsetOfBoundaryVoxel(
        const Vector3i &Xlocal);
    __DEVICE__ inline int LinearizeNeighborOffset(const Vector3i &dXsv);
    __DEVICE__ inline Vector3i BoundaryVoxelInNeighbor(
        const Vector3i &Xlocal, const Vector3i &dXsv);

    __DEVICE__ void CacheNeighborSubvolumes(
        const Vector3i &Xsv, const Vector3i &dXsv,
        int *cached_subvolume_indices,
        UniformTSDFVolumeCudaDevice **cached_subvolumes);

    /** In these functions range of input indices are [-1, N+1)
     * (xlocal, ylocal, zlocal) is inside
     * cached_subvolumes[IndexOfNeighborSubvolumes(0, 0, 0)] **/
    __DEVICE__ Vector3f gradient(
        const Vector3i &Xlocal,
        UniformTSDFVolumeCudaDevice **cached_subvolumes);
    __DEVICE__ float TSDFOnBoundaryAt(
        const Vector3f &Xlocal,
        UniformTSDFVolumeCudaDevice **cached_subvolumes);
    __DEVICE__ uchar WeightOnBoundaryAt(
        const Vector3f &Xlocal,
        UniformTSDFVolumeCudaDevice **cached_subvolumes);
    __DEVICE__ Vector3b ColorOnBoundaryAt(
        const Vector3f &Xlocal,
        UniformTSDFVolumeCudaDevice **cached_subvolumes);
    __DEVICE__ Vector3f GradientOnBoundaryAt(
        const Vector3f &Xlocal,
        UniformTSDFVolumeCudaDevice **cached_subvolumes);

public:
    __DEVICE__ void TouchSubvolume(const Vector2i &p,
                                   ImageCudaDevice<float, 1> &depth,
                                   PinholeCameraIntrinsicCuda &camera,
                                   TransformCuda &transform_camera_to_world);
    __DEVICE__ void Integrate(const Vector3i &Xlocal,
                              HashEntry<Vector3i> &target_subvolume_entry,
                              RGBDImageCudaDevice &rgbd,
                              PinholeCameraIntrinsicCuda &camera,
                              TransformCuda &transform_camera_to_world);
    __DEVICE__ Vector3f RayCasting(const Vector2i &p,
                                   PinholeCameraIntrinsicCuda &camera,
                                   TransformCuda &transform_camera_to_world);
    __DEVICE__ Vector3f VolumeRendering(const Vector2i &p,
                                        PinholeCameraIntrinsicCuda &camera,
                                        TransformCuda &transform_camera_to_world);

public:
    friend class ScalableTSDFVolumeCuda;
};

struct ScalableTSDFVolumeCpuData {
public:
    std::vector<float> tsdf_;
    std::vector<uchar> weight_;
    std::vector<Vector3b> color_;

    ScalableTSDFVolumeCpuData() = default;
    ScalableTSDFVolumeCpuData(
        std::vector<float> tsdf_buffer,
        std::vector<uchar> weight_buffer,
        std::vector<Vector3b> color_buffer) :
        tsdf_(std::move(tsdf_buffer)),
        weight_(std::move(weight_buffer)),
        color_(std::move(color_buffer)) {};
};


class ScalableTSDFVolumeCuda {
public:
    /** Note here the template is exactly the same as the
     * SpatialHashTableCudaDevice.
     * We will explicitly deal with the UniformTSDFVolumeCudaDevice later
     * **/
    typedef HashTableCuda
    <Vector3i, UniformTSDFVolumeCudaDevice, SpatialHasher>
        SpatialHashTableCuda;
    std::shared_ptr<ScalableTSDFVolumeCudaDevice> device_ = nullptr;

public:
    int N_;

    SpatialHashTableCuda hash_table_;
    ArrayCuda<HashEntry<Vector3i>> active_subvolume_entry_array_;

public:
    int bucket_count_;
    int value_capacity_;

    float voxel_length_;
    float sdf_trunc_;
    TransformCuda transform_volume_to_world_;

public:
    ScalableTSDFVolumeCuda();
    ScalableTSDFVolumeCuda(int N, float voxel_length, float sdf_trunc,
                           const TransformCuda &transform_volume_to_world
                           = TransformCuda::Identity(),
                           /* 20,000 buckets, 200,000 + 200,000 entries */
                           int bucket_count = 20000,
                           /* 400,000 entries -> 400,000 values */
                           int value_capacity = 400000);
    ScalableTSDFVolumeCuda(const ScalableTSDFVolumeCuda &other);
    ScalableTSDFVolumeCuda &operator=(const ScalableTSDFVolumeCuda &other);
    ~ScalableTSDFVolumeCuda();

    /** BE CAREFUL, we have to rewrite some
     * non-wrapped allocation stuff here for UniformTSDFVolumeCudaDevice **/
    void Create(int N, int bucket_count, int value_capacity);
    void Release();
    void Reset();
    void UpdateDevice();


    std::vector<Vector3i> DownloadKeys();
    std::pair<std::vector<Vector3i>,
              std::vector<ScalableTSDFVolumeCpuData>> DownloadVolumes();

    /** Return addr index in cuda **/
    std::vector<int> UploadKeys(std::vector<Vector3i> &keys);
    bool UploadVolumes(std::vector<Vector3i> &key,
                       std::vector<ScalableTSDFVolumeCpuData> &volume);

public:
    /** Hash_table based integration is non-trivial,
     *  it requires 3 passes: pre-allocation, get volumes, and integration
     *  NOTE: we cannot merge stage 1 and 2:
     *  - TouchBlocks allocate blocks in parallel.
     *    If we return only newly allocated volumes, then we fail to capture
     *    already allocated volumes.
     *  - If we capture all the allocated volume indices in parallel, then
     *    there will be duplicates. (thread1 allocate and return, thread2
     *    capture it and return again).
     **/
    void TouchSubvolumes(ImageCuda<float, 1> &depth,
                         PinholeCameraIntrinsicCuda &camera,
                         TransformCuda &transform_camera_to_world);
    void GetSubvolumesInFrustum(PinholeCameraIntrinsicCuda &camera,
                                TransformCuda &transform_camera_to_world);
    void GetAllSubvolumes();
    void IntegrateSubvolumes(RGBDImageCuda &rgbd,
                             PinholeCameraIntrinsicCuda &camera,
                             TransformCuda &transform_camera_to_world);

    void ResetActiveSubvolumeIndices();

    void Integrate(RGBDImageCuda &rgbd,
                   PinholeCameraIntrinsicCuda &camera,
                   TransformCuda &transform_camera_to_world);
    void RayCasting(ImageCuda<float, 3> &image,
                    PinholeCameraIntrinsicCuda &camera,
                    TransformCuda &transform_camera_to_world);
    void VolumeRendering(ImageCuda<float, 3> &image,
                         PinholeCameraIntrinsicCuda &camera,
                         TransformCuda &transform_camera_to_world);

    ScalableTSDFVolumeCuda DownSample();
};


class ScalableTSDFVolumeCudaKernelCaller {
public:
    static void Create(ScalableTSDFVolumeCuda &volume);

    static void TouchSubvolumes(ScalableTSDFVolumeCuda &volume,
                                ImageCuda<float, 1> &depth,
                                PinholeCameraIntrinsicCuda &camera,
                                TransformCuda &transform_camera_to_world);

    static void IntegrateSubvolumes(ScalableTSDFVolumeCuda &volume,
                                    RGBDImageCuda &rgbd,
                                    PinholeCameraIntrinsicCuda &camera,
                                    TransformCuda &transform_camera_to_world);

    static void GetSubvolumesInFrustum(ScalableTSDFVolumeCuda &volume,
                                       PinholeCameraIntrinsicCuda &camera,
                                       TransformCuda &transform_camera_to_world);

    static void GetAllSubvolumes(ScalableTSDFVolumeCuda &volume);

    static void RayCasting(ScalableTSDFVolumeCuda &volume,
                           ImageCuda<float, 3> &normal,
                           PinholeCameraIntrinsicCuda &camera,
                           TransformCuda &transform_camera_to_world);

    static void VolumeRendering(ScalableTSDFVolumeCuda &volume,
                                ImageCuda<float, 3> &normal,
                                PinholeCameraIntrinsicCuda &camera,
                                TransformCuda &transform_camera_to_world);

    static void DownSample(ScalableTSDFVolumeCuda &volume,
                           ScalableTSDFVolumeCuda &volume_down);
};


__GLOBAL__
void CreateKernel(ScalableTSDFVolumeCudaDevice device);


__GLOBAL__
void TouchSubvolumesKernel(ScalableTSDFVolumeCudaDevice device,
                           ImageCudaDevice<float, 1> depth,
                           PinholeCameraIntrinsicCuda camera,
                           TransformCuda transform_camera_to_world);


__GLOBAL__
void IntegrateSubvolumesKernel(ScalableTSDFVolumeCudaDevice device,
                               RGBDImageCudaDevice depth,
                               PinholeCameraIntrinsicCuda camera,
                               TransformCuda transform_camera_to_world);


__GLOBAL__
void GetSubvolumesInFrustumKernel(ScalableTSDFVolumeCudaDevice device,
                                  PinholeCameraIntrinsicCuda camera,
                                  TransformCuda transform_camera_to_world);


__GLOBAL__
void GetAllSubvolumesKernel(ScalableTSDFVolumeCudaDevice device);


__GLOBAL__
void RayCastingKernel(ScalableTSDFVolumeCudaDevice device,
                      ImageCudaDevice<float, 3> vertex,
                      PinholeCameraIntrinsicCuda camera,
                      TransformCuda transform_camera_to_world);

__GLOBAL__
void VolumeRenderingKernel(ScalableTSDFVolumeCudaDevice device,
                           ImageCudaDevice<float, 3> color,
                           PinholeCameraIntrinsicCuda camera,
                           TransformCuda transform_camera_to_world);


__GLOBAL__
void DownSampleKernel(ScalableTSDFVolumeCudaDevice device,
                      ScalableTSDFVolumeCudaDevice device_down);

} // cuda
} // open3d