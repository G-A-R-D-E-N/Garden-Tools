# Fallout 4 Plugin Parse Memory

This report measures 'Fallout4.esm' with a warmed file cache. Both runs use
Clang 21.1.8, Debug configuration, C++23, and the 'Radicle/build-memory'
profiling harness. The baseline is unchanged 'main' at '5e8a5ec'.

## Commands

Baseline build:

    cmake -S Radicle -B Radicle/build-memory \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=clang++
    cmake --build Radicle/build-memory --target fo4 --parallel

The full target also includes 'radicle_c', but this NTFS worktree cannot
create the versioned shared-library symlinks required by that target. The
measurement harness links directly to 'libfo4.a'. Test execution used an
equivalent Debug Clang build under '/tmp/radicle-memory-test-build' on ext4.

Measured process:

    dd if="/run/media/user/Games-Storage/SteamLibrary/steamapps/common/Fallout 4/Data/Fallout4.esm" \
      of=/dev/null bs=16M status=none
    Radicle/build-memory/measure_memory \
      "/run/media/user/Games-Storage/SteamLibrary/steamapps/common/Fallout 4/Data/Fallout4.esm"

## Results

| Metric | Baseline | Shared backing |
|---|---:|---:|
| File size | 330,776,576 bytes | 330,776,576 bytes |
| Group count | 112,381 | 112,381 |
| Record count including TES4 | 1,549,277 | 1,549,277 |
| Compressed record count | 56,399 | 56,399 |
| Subrecord count | 6,743,366 | 6,743,366 |
| Parse wall time | 5,138 ms | 4,257 ms |
| VmRSS after parse | 3,860,880 KiB | 1,905,100 KiB |
| VmHWM | 4,183,412 KiB | 1,921,528 KiB |

The shared-backing run is 17.1% faster in this paired warmed-cache
measurement. Its peak RSS is below the 2.5 GB acceptance target.

## Baseline retained allocations

All listed byte-vector capacities were equal to their logical sizes:

| Allocation | Bytes |
|---|---:|
| Plugin original bytes | 330,776,576 |
| Group original copies | 1,653,159,335 |
| Record original copies | 328,079,432 |
| Decompressed payloads | 368,737,740 |
| Subrecord data | 447,715,631 |
| GroupChild object capacity | 354,705,224 |
| Subrecord object capacity | 349,568,000 |

The baseline retained allocation model totals 3,832,760,370 bytes. The
post-parse RSS remainder is approximately 115 MiB and is not attributed more
specifically than allocator, library, stack, and mapping overhead. The peak
HWM includes the temporary 330,776,576-byte reader vector that existed while
'read_plugin_file' passed its data to 'read_plugin'.

## Shared-backing allocations

The implementation keeps one immutable 330,776,576-byte plugin source buffer.
Records retain only an offset, length, and backing-store identity into that
source. Group preservation copies are removed. The remaining measured
byte-vector and object allocations are:

| Allocation | Capacity |
|---|---:|
| Plugin backing bytes | 330,776,576 |
| Decompressed payloads | 368,737,740 |
| Subrecord data | 447,715,631 |
| GroupChild object capacity | 354,705,224 |
| Subrecord object capacity | 349,568,000 |
| Top-level group object capacity | 12,288 |

The modeled live allocation total is 1,851,515,459 bytes, with approximately
95 MiB of measured RSS remainder after parse. The peak RSS remainder is
approximately 111 MiB.

## Verification

Safe suite:

    env -u FO4_DATA_DIR -u FO4_PLUGIN_CORPUS \
      /tmp/radicle-memory-test-build/fo4_tests

Result: 315 passed, 19 skipped real-data tests.

Fallout 4 data filters:

    FO4_DATA_DIR="/run/media/user/Games-Storage/SteamLibrary/steamapps/common/Fallout 4/Data" \
      /tmp/radicle-memory-test-build/fo4_tests \
      --gtest_filter='RealPluginPreservation.UnmodifiedFallout4MasterIsByteIdentical:ProjExplHazdReencodeRealData.ReproducesEveryShippedPayloadByteForByte'

Result: 2 passed.

Corpus filters:

    FO4_PLUGIN_CORPUS="/run/media/user/Games-Storage/Modlists/Fallen World Alpha 2" \
      /tmp/radicle-memory-test-build/fo4_tests \
      --gtest_filter='PluginCorpus.EveryDecoderSurvivesThirdPartyPlugins:PluginCorpus.EveryUnmodifiedPluginWritesByteIdentically'

Result: 2 passed. The corpus contained 869 plugins. The decoder sweep made
25,811 decode attempts.
