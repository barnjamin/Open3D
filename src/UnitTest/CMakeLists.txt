cmake_minimum_required(VERSION 3.0)

include_directories(".")

#if (BUILD_CUDA_MODULE)
#    message(STATUS "Building Cuda UnitTests")
#    add_subdirectory(Cuda)
#endif()

#target_link_libraries(unitTests pthread ${CMAKE_PROJECT_NAME})
# > This is too general and lacks scalability
# > We test our separated files in Cuda/
#file(GLOB_RECURSE UNIT_TESTS "*.cpp")
#add_executable(unitTests ${UNIT_TESTS})
#
#target_link_libraries(unitTests ${GTEST_LIBRARIES} pthread
#        ${CMAKE_PROJECT_NAME})
