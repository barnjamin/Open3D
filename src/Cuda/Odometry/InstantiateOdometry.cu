//
// Created by wei on 10/4/18.
//

#include "RGBDOdometryCuda.h"
#include "RGBDOdometryCudaDevice.cuh"
#include "RGBDOdometryCudaKernel.cuh"

#include "Reduction2DCudaKernel.cuh"

#include <Cuda/Geometry/ImageCudaDevice.cuh>
#include <Cuda/Geometry/ImageCudaKernel.cuh>

namespace open3d {
namespace cuda {
template
class RGBDOdometryCudaDevice<3>;
template
class RGBDOdometryCudaKernelCaller<3>;

template
class ImageCudaDevice<float, 6>;
template
class ImageCudaKernelCaller<float, 6>;

template
float ReduceSum2D<float, 1>(ImageCuda<float, 1> &src);

template
float ReduceSum2DShuffle<float, 1>(ImageCuda<float, 1> &src);

template
float AtomicSum<float, 1>(ImageCuda<float, 1> &src);

template
int ReduceSum2D<int, 1>(ImageCuda<int, 1> &src);

template
int ReduceSum2DShuffle<int, 1>(ImageCuda<int, 1> &src);

template
int AtomicSum<int, 1>(ImageCuda<int, 1> &src);

} // cuda
} // open3d