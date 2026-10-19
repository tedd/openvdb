# Broader ValueAccessor cache comparison — 2026-10-19

The cache improves fitting working sets, but the broader comparison identifies material regressions. The measurements do not support adopting it as a universally faster default. The selected correctness suites passed.

Measured baseline: `84dca76d5759b0ca8d4d30aa473437630a357c4f` (OpenVDB 13.1.1). Measured port: `77990f081de41173bcbb26762bfeeea2ca6ee848`. Later reporting and lock-path changes do not change the measured C++ implementation. Source and binary hashes identify both arms in [results.json](results.json).

The run contains **75 case pairs, 242 processes, and 2,870 timed samples** over four value types and nine accessor fixtures, plus seven float algorithm/lifecycle cases. Baseline correctness: **216/216**; port correctness: **219/219**, including three new regression tests; **18 selected upstream suites**, zero failures or disabled tests. This is not the entire upstream suite.

## Observations

Elapsed changes below are relative to the matching unmodified native API. Positive is slower.

- Coherent 64-leaf reads improve 27.4–33.7%; writes improve 31.4–37.6% across the four types.
- Shuffled 64-leaf reads improve 76.9–79.2%; writes improve 77.9–79.0%.
- Coherent 512-leaf reads regress 12.0–23.3%; writes are approximately unchanged in this build.
- Shuffled 512-leaf reads regress 29.3–37.8%; writes regress 24.0–30.0%.
- Deliberate-collision reads regress 16.8–25.1%; writes regress 6.9–9.3%.
- Accessor construction/read/destruction regresses 23.9%; clear/read regresses 48.1%.
- The default Tree4 accessor grows from **96 to 608 bytes**, an unconditional **512-byte** array. The storage is also present for packed, mutex-backed, and non-leaf-caching accessor instantiations. This changes object layout and requires rebuilding consumers.
- Box/quadratic sampling, probes, and dense copying have small positive median changes (+1.3–3.7%); mean filtering improves 5.7%. Consult ranges before interpreting small differences.
- Churn varies substantially between processes and value types. Its median direction is inconsistent; the complete ranges and paired observations are retained.

A primary miss now performs a secondary-slot lookup before the existing traversal. A full signed origin comparison prevents collision hits from returning another leaf. Registered invalidation and explicit leaf erasure clear the entire secondary cache. The fitting-cache gains coexist with added miss and lifecycle work; these measurements do not isolate the contribution of instruction layout from cache overhead.

## Environment and protocol

AMD Ryzen 9 5950X, Windows 11 x64 build 26200, MSVC 19.52.37025, C++17, static Release `/O2 /MT`, oneTBB 2022.2.0, zlib 1.3.2, GoogleTest 1.17.0. Both core libraries were rebuilt separately. OpenVDB explicit instantiation is off and its SIMD setting is `None`. Assertions are on in both builds, with an identical build-only `__PRETTY_FUNCTION__=__FUNCSIG__` mapping for the baseline MSVC assertion macro. Blosc is off. The existing High performance power scheme was retained.

Every process uses logical CPU 16. Process order and read/write order alternate. Setup, population, warmup, and validation are outside timing. Scalar reads include UInt64 checksum accumulation; writes change values. Churn includes deletion/allocation. Accessor construction and clearing are explicitly timed. Mean-filter input copying is outside timing. Native allocations are not instrumented.

Coherent64 uses five processes per build/type, nine samples of 16,777,216 operations, and 1,000 ms warmup per operation. Other accessor cases use three processes, five samples of 8,388,608 operations, and 500 ms warmup; churn uses 65,536 operations. Algorithm cases use five processes and five timed samples after two untimed calls. Sampling/probe/lifecycle cases execute 1,048,576 scalar operations. Dense copying executes 256 serial 32³ copies per sample; filtering executes eight serial width-one iterations over a fresh 32³ grid.

The table reports the **median of process medians**, with the full process-median range in parentheses. Dense-copy units are per voxel copied; filter units are per voxel per iteration. All other rows report one completed scalar/lifecycle/churn operation. All final observations are retained. Shared-host measurements are descriptive; ranges are not confidence intervals. No statistical significance or application-wide conclusion is inferred. Default assertion-disabled Release, other compilers/CPUs, file I/O, and production volumes were not measured.

See [README.md](README.md) for fixture definitions and reproduction commands. The JSON includes every final sample, launch medians, paired ratios, correctness-suite summaries, long-loop preflights, and environment/source/binary identities. An initial partial run was stopped to correct float checksum accumulation in the benchmark. Its completed observations are retained separately under `supersededPilot` and are excluded from the final table; none were selectively reused.

## Complete measurements

| Type | Fixture | Operation | Baseline ns/op (launch range) | Port ns/op (launch range) | Elapsed change |
|---|---|---|---:|---:|---:|
| u16 | singleleaf | read | 2.098 (2.096–2.103) | 1.925 (1.923–1.926) | -8.2% |
| u16 | singleleaf | write | 3.566 (3.559–3.570) | 2.947 (2.941–3.022) | -17.4% |
| u16 | coherent64 | read | 3.649 (3.622–3.762) | 2.419 (2.418–2.421) | -33.7% |
| u16 | coherent64 | write | 4.957 (4.894–4.998) | 3.401 (3.393–3.440) | -31.4% |
| u16 | random64 | read | 32.827 (32.765–34.181) | 6.834 (6.830–6.835) | -79.2% |
| u16 | random64 | write | 37.260 (35.838–37.300) | 7.958 (7.900–8.006) | -78.6% |
| u16 | coherent512 | read | 3.383 (3.382–3.387) | 3.954 (3.947–3.984) | +16.9% |
| u16 | coherent512 | write | 4.694 (4.693–4.709) | 4.630 (4.590–4.653) | -1.4% |
| u16 | random512 | read | 32.345 (32.190–33.776) | 44.578 (44.191–46.307) | +37.8% |
| u16 | random512 | write | 39.105 (37.782–39.657) | 48.476 (45.855–49.556) | +24.0% |
| u16 | partial64 | read | 3.617 (3.605–3.617) | 2.424 (2.419–2.428) | -33.0% |
| u16 | partial64 | write | 6.502 (6.495–6.545) | 3.840 (3.836–3.843) | -40.9% |
| u16 | collision64 | read | 16.350 (16.339–16.418) | 20.078 (20.061–20.125) | +22.8% |
| u16 | collision64 | write | 20.542 (20.412–20.657) | 22.166 (21.852–22.191) | +7.9% |
| u16 | root64 | read | 2.237 (2.230–2.240) | 2.081 (2.080–2.086) | -7.0% |
| u16 | root64 | write | 3.743 (3.701–3.772) | 3.116 (3.101–3.140) | -16.7% |
| u16 | churn | write | 595.287 (465.257–596.320) | 485.300 (484.747–486.633) | -18.5% |
| float | singleleaf | read | 2.449 (2.448–2.454) | 2.403 (2.398–2.417) | -1.9% |
| float | singleleaf | write | 3.509 (3.507–3.529) | 2.691 (2.669–2.905) | -23.3% |
| float | coherent64 | read | 4.096 (4.069–4.244) | 2.913 (2.909–2.916) | -28.9% |
| float | coherent64 | write | 4.956 (4.836–5.086) | 3.093 (3.086–3.109) | -37.6% |
| float | random64 | read | 33.741 (33.697–36.250) | 7.793 (7.783–7.795) | -76.9% |
| float | random64 | write | 37.167 (36.073–38.058) | 8.046 (8.020–8.050) | -78.4% |
| float | coherent512 | read | 3.984 (3.980–3.989) | 4.462 (4.439–4.473) | +12.0% |
| float | coherent512 | write | 4.617 (4.594–4.629) | 4.605 (4.596–4.722) | -0.3% |
| float | random512 | read | 33.915 (33.802–36.355) | 43.858 (43.827–45.662) | +29.3% |
| float | random512 | write | 40.910 (37.553–43.545) | 51.474 (47.369–51.628) | +25.8% |
| float | partial64 | read | 4.074 (4.036–4.077) | 2.912 (2.908–2.912) | -28.5% |
| float | partial64 | write | 6.673 (6.669–6.680) | 3.812 (3.808–3.832) | -42.9% |
| float | collision64 | read | 17.836 (17.759–17.998) | 20.831 (20.663–20.835) | +16.8% |
| float | collision64 | write | 20.423 (20.291–20.665) | 21.824 (21.802–21.942) | +6.9% |
| float | root64 | read | 2.655 (2.620–2.659) | 2.587 (2.582–2.627) | -2.6% |
| float | root64 | write | 3.722 (3.673–3.730) | 2.887 (2.870–2.913) | -22.4% |
| float | churn | write | 542.726 (463.658–672.424) | 612.039 (516.200–644.028) | +12.8% |
| double | singleleaf | read | 2.453 (2.453–2.456) | 2.406 (2.405–2.408) | -1.9% |
| double | singleleaf | write | 3.509 (3.504–3.524) | 2.676 (2.665–2.678) | -23.7% |
| double | coherent64 | read | 4.017 (3.999–5.180) | 2.918 (2.905–2.921) | -27.4% |
| double | coherent64 | write | 4.972 (4.867–5.109) | 3.142 (3.127–3.159) | -36.8% |
| double | random64 | read | 33.728 (33.702–34.695) | 7.672 (7.556–7.694) | -77.3% |
| double | random64 | write | 37.612 (35.868–40.330) | 8.325 (8.304–8.340) | -77.9% |
| double | coherent512 | read | 4.107 (4.106–4.133) | 4.616 (4.602–4.618) | +12.4% |
| double | coherent512 | write | 4.616 (4.614–4.634) | 4.617 (4.611–4.622) | +0.0% |
| double | random512 | read | 34.032 (33.952–35.160) | 44.524 (44.114–46.822) | +30.8% |
| double | random512 | write | 39.856 (37.895–40.117) | 50.679 (47.744–51.153) | +27.2% |
| double | partial64 | read | 3.986 (3.972–4.003) | 2.911 (2.910–2.918) | -27.0% |
| double | partial64 | write | 6.712 (6.691–6.763) | 3.877 (3.862–3.879) | -42.2% |
| double | collision64 | read | 17.907 (17.814–18.035) | 21.166 (21.096–21.411) | +18.2% |
| double | collision64 | write | 20.574 (20.336–21.104) | 22.450 (22.203–22.711) | +9.1% |
| double | root64 | read | 2.653 (2.641–2.663) | 2.588 (2.587–2.608) | -2.5% |
| double | root64 | write | 3.690 (3.667–3.698) | 2.882 (2.876–2.886) | -21.9% |
| double | churn | write | 526.120 (524.850–665.987) | 597.455 (464.568–644.969) | +13.6% |
| int64 | singleleaf | read | 2.101 (2.100–2.122) | 1.924 (1.922–1.927) | -8.4% |
| int64 | singleleaf | write | 3.519 (3.501–3.521) | 2.677 (2.670–2.683) | -23.9% |
| int64 | coherent64 | read | 3.641 (3.626–4.324) | 2.427 (2.423–2.445) | -33.3% |
| int64 | coherent64 | write | 4.915 (4.896–5.559) | 3.105 (3.082–3.116) | -36.8% |
| int64 | random64 | read | 33.603 (33.366–34.617) | 7.041 (7.032–7.168) | -79.0% |
| int64 | random64 | write | 37.467 (36.670–37.685) | 7.885 (7.828–7.891) | -79.0% |
| int64 | coherent512 | read | 3.447 (3.435–3.462) | 4.251 (4.002–4.271) | +23.3% |
| int64 | coherent512 | write | 4.627 (4.617–4.636) | 4.627 (4.625–4.632) | -0.0% |
| int64 | random512 | read | 33.347 (33.342–34.808) | 45.460 (45.361–47.071) | +36.3% |
| int64 | random512 | write | 39.085 (38.145–40.290) | 50.809 (48.408–51.580) | +30.0% |
| int64 | partial64 | read | 3.639 (3.631–3.647) | 2.428 (2.426–2.428) | -33.3% |
| int64 | partial64 | write | 6.472 (6.447–6.553) | 3.688 (3.684–3.695) | -43.0% |
| int64 | collision64 | read | 16.890 (16.887–16.974) | 21.122 (20.920–21.160) | +25.1% |
| int64 | collision64 | write | 20.546 (20.453–20.636) | 22.454 (22.194–22.640) | +9.3% |
| int64 | root64 | read | 2.274 (2.272–2.283) | 2.101 (2.099–2.114) | -7.6% |
| int64 | root64 | write | 3.668 (3.645–3.679) | 2.879 (2.858–2.886) | -21.5% |
| int64 | churn | write | 678.973 (539.760–679.439) | 545.229 (473.282–627.634) | -19.7% |
| float | algorithms | box-sampling | 57.327 (57.315–57.397) | 58.186 (58.146–58.307) | +1.5% |
| float | algorithms | quadratic-sampling | 198.483 (197.918–199.589) | 201.048 (200.620–201.775) | +1.3% |
| float | algorithms | probe-value | 7.160 (7.145–7.209) | 7.356 (7.328–7.387) | +2.7% |
| float | algorithms | accessor-create-read-destroy | 90.051 (89.874–91.009) | 111.563 (111.446–111.718) | +23.9% |
| float | algorithms | accessor-clear-read | 25.814 (25.748–26.388) | 38.229 (37.831–38.697) | +48.1% |
| float | algorithms | copy-to-dense | 0.712 (0.663–0.749) | 0.739 (0.730–0.742) | +3.7% |
| float | algorithms | mean-filter | 60.211 (60.120–60.751) | 56.800 (56.446–58.353) | -5.7% |
