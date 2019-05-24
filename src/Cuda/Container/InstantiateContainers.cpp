//
// Created by wei on 11/9/18.
//

#include "ArrayCudaHost.hpp"
#include "Array2DCudaHost.hpp"
#include "HashTableCudaHost.hpp"
#include "MemoryHeapCudaHost.hpp"
#include "LinkedListCudaHost.hpp"

namespace open3d {

namespace cuda {
template
class ArrayCuda<int>;
template
class ArrayCuda<float>;
template
class ArrayCuda<Vector3i>;
template
class ArrayCuda<Vector4i>;
template
class ArrayCuda<Vector3f>;
template
class ArrayCuda<Vector2i>;
template
class ArrayCuda<HashEntry<Vector3i>>;
template
class ArrayCuda<LinkedListCudaDevice<HashEntry<Vector3i>>>;

template
class Array2DCuda<int>;
template
class Array2DCuda<float>;

template
class MemoryHeapCuda<int>;
template
class MemoryHeapCuda<float>;
template
class MemoryHeapCuda<LinkedListNodeCuda<int>>;

template
class HashTableCuda<Vector3i, int, SpatialHasher>;

template
class LinkedListCuda<int>;

} // cuda
} // open3d