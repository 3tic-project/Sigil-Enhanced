# OpenCC vendoring notes

Sigil Enhanced vendors the build-required subset of the official OpenCC 1.3.1
release (`ver.1.3.1`). The upstream release archive SHA-256 is:

```text
1cc663704ff15728d6ea41ced8cd9dcc086f7bd9a80e8531b2f8054d2f3b8733
```

The unneeded documentation, language bindings, command-line tools, tests,
benchmarks, Darts backend, and experimental Jieba plugin are omitted.
`CMakeLists.txt` is adjusted to pin the release version and skip those omitted
targets. MSVC warning options are restricted to C++ compilation so the Windows
resource compiler does not receive unsupported compiler switches. The `.ocd2`
dictionaries were generated once from the unmodified
1.3.1 release data with its `opencc_dict` tool and are portable OpenCC runtime
assets. OpenCC and its data are licensed under Apache-2.0; see `LICENSE` and the
upstream dependency licenses in `deps/`.
