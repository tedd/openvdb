# Copyright Contributors to the OpenVDB Project
# SPDX-License-Identifier: Apache-2.0
# Include with -DCMAKE_PROJECT_OpenVDB_INCLUDE=<absolute path to this file>.
if(MSVC)
  # The baseline assertion macro uses the GCC spelling on Windows.
  add_compile_definitions(__PRETTY_FUNCTION__=__FUNCSIG__)
endif()
foreach(kind u16 float double int64)
  add_executable(cache_${kind} "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
  target_compile_features(cache_${kind} PRIVATE cxx_std_17)
  target_compile_definitions(cache_${kind} PRIVATE BENCH_${kind} NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(cache_${kind} PRIVATE openvdb)
endforeach()
add_executable(algorithms "${CMAKE_CURRENT_LIST_DIR}/algorithms.cpp")
target_compile_features(algorithms PRIVATE cxx_std_17)
target_compile_definitions(algorithms PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(algorithms PRIVATE openvdb)
