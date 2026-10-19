# ValueAccessor secondary-cache benchmark

This benchmark compares the same native `getValue` and `setValueOn` APIs in a stock OpenVDB build and a build with a 64-entry secondary leaf cache and separate primary-hit/miss paths. It also measures accessor construction, clearing, probes, interpolation, dense copying, and filtering. The benchmark targets Windows x64 and uses no benchmark framework.

## Workloads and validation

- `singleleaf`: 512 active voxels in one 8Â³ leaf.
- `coherent64`: a signed-coordinate 32Â³ active cube, spanning 64 leaves. Z changes fastest; seven of eight successive accesses remain in the same leaf. All leaf origins occupy distinct secondary slots.
- `random64`: the same cube after a deterministic shuffle.
- `coherent512` and `random512`: a signed-coordinate 64Â³ active cube, spanning 512 leaves, in coherent or shuffled order. Eight leaf origins alias each secondary slot.
- `partial64`: the 32Â³ cube with alternating active/inactive voxels. Writes update only the active half; reads include both halves.
- `collision64`: 64 leaves whose origins all hash to the same secondary slot; shuffled voxel order.
- `root64`: 64 leaves spaced 4,096 voxels apart along X, spanning different root entries.
- `churn`: steal, destroy, and recreate one leaf, including allocation and registered-accessor invalidation in the interval.

The accessor fixtures use UInt16, float, double, and Int64 Tree4 5/4/3 values. Coordinate generation, initial population, checksums, complete value/activity validation, and topology counts are outside each timed interval. The read loop includes conversion to a UInt64 checksum accumulator. Write values vary by sample. Every read checksum is checked against an independent formula; every write sample is checked through direct tree lookup. Native allocations are not instrumented.

The float algorithm fixture contains the linear field `100 + x + 2y + 3z` over the 32Â³ cube. Box and quadratic sampling use quarter-voxel offsets in the interior; interpolation checks the analytical linear value. Probes and accessor lifecycle kernels check the complete accumulated value. Dense copying repeats a serial whole-grid copy 256 times per sample. Mean filtering runs eight serial width-one iterations on a fresh deep copy; the copy is outside timing. Dense values, filter activity, the central linear value, and aggregate checksums are validated. Checksums are also compared between builds.

All timing uses `std::chrono::steady_clock`. Each process is pinned to logical CPU 16. Process order and read/write order alternate. Coherent64 uses five processes per build/type, nine samples of 16,777,216 operations and 1,000 ms warmup per operation. Other accessor fixtures use three processes, five samples of 8,388,608 operations, and 500 ms warmup; churn uses 65,536 operations. Algorithm cases use five processes per build, two untimed calls, and five timed samples. Sampling/probe/lifecycle cases use 1,048,576 operations per sample.

Results report the median of process medians, with the full range of process medians. Positive elapsed change means slower. Observations are descriptive: the host is shared, and CPU snapshots do not establish uninterrupted idleness. No statistical significance or application-wide conclusion is inferred. No observations from the final protocol are discarded.

## Reproduction

Requires MSVC x64, CMake, Python on Windows, oneTBB 2022.2.0, zlib 1.3.2, and GoogleTest 1.17.0 built with `/MT`. Stage dependencies and all builds under a dedicated `D:/Workspaces/AI/<task>` directory. The supplied `Targets.cmake` injects the identical benchmark callers into each native build.

Use stock commit `84dca76d5759b0ca8d4d30aa473437630a357c4f` and measured port commit `77990f08` in separate source directories. The latter changes the accessor implementation and the benchmark checksum conversion; later result commits do not change the measured C++ implementation. Set `$stockSource`, `$cacheSource`, `$work`, `$tbb`, `$gtest`, `$zlibInclude`, and `$zlibLibrary` to their absolute staged paths. Run this from the port checkout:

```powershell
$bench = (Resolve-Path benchmarks/accessor-cache).Path
$tests = 'ValueAccessor;Tree;TreeGetSetValues;TreeIterators;TreeCombine;Leaf;LeafBool;LeafMask;LeafManager;Dense;QuadraticInterp;Stencils;Filter;GridTransformer;VolumeToMesh;VolumeRayIntersector;PointDataLeaf;PointSample'
$env:CL = '/MP4'
foreach ($variant in 'stock','cache') {
    $source = if ($variant -eq 'stock') { $stockSource } else { $cacheSource }
    cmake -S $source -B "$work/$variant-build" -G 'Visual Studio 18 2026' -A x64 `
      "-DCMAKE_PROJECT_OpenVDB_INCLUDE=$bench/Targets.cmake" `
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
      -DOPENVDB_BUILD_BINARIES=OFF -DOPENVDB_BUILD_UNITTESTS=ON `
      "-DOPENVDB_TESTS=$tests" -DOPENVDB_CORE_SHARED=OFF -DOPENVDB_CORE_STATIC=ON `
      -DUSE_BLOSC=OFF -DUSE_ZLIB=ON -DUSE_IMATH_HALF=OFF -DUSE_EXR=OFF `
      -DUSE_CCACHE=OFF -DUSE_EXPLICIT_INSTANTIATION=OFF `
      -DCONCURRENT_MALLOC=None -DOPENVDB_SIMD=None -DOPENVDB_ENABLE_ASSERTS=ON `
      "-DTBB_ROOT=$tbb" "-DTBB_LIBRARYDIR=$tbb/lib/intel64/vc14" `
      "-DGTest_DIR=$gtest" "-DZLIB_INCLUDE_DIR=$zlibInclude" "-DZLIB_LIBRARY=$zlibLibrary"
    cmake --build "$work/$variant-build" --config Release --parallel 3
}
$common = @('--workspace', "$work/measure", '--stock-build', "$work/stock-build",
    '--cache-build', "$work/cache-build", '--stock-source', $stockSource,
    '--cache-source', $cacheSource, '--tbb-root', $tbb, '--cpu', '16')
python "$bench/compare.py" @common --tests
python "$bench/compare.py" @common
```

Assertions are enabled in both Release builds. The injected build-only `__PRETTY_FUNCTION__=__FUNCSIG__` definition permits the baseline assertion macro to compile on MSVC. It is unrelated to the cache implementation. Blosc is disabled; delayed I/O has been removed in this baseline. The core library is rebuilt separately for each variant to avoid mixing accessor layouts.

The measured run held an existing shared host performance lock throughout timing. The current driver accepts `--lock-file <existing-file>` for host-wide coordination; its default lock is local to the measurement workspace. The later lock-path option does not change either measured C++ caller. Raw JSON files include source and binary hashes, compiler identification, affinity, power scheme, process-level observations, and CPU snapshots. Use a fresh measurement directory for every complete run.
