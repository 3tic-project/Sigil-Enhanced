# Bundled HarfBuzz

Sigil vendors HarfBuzz so that font subsetting is available on every supported
platform without depending on a system HarfBuzz installation.

- Upstream version: 14.2.1
- Release date: 2026-06-02
- Source archive: https://github.com/harfbuzz/harfbuzz/releases/download/14.2.1/harfbuzz-14.2.1.tar.xz
- SHA-256: `a54a5d8e9380a41fbb762ce367bcbf7704792dfca0d93f1bbca86c5a57902e0e`
- License: MIT; see `COPYING`

The upstream `src` directory and the root files required by its CMake build are
kept unchanged. This includes `meson.build`: upstream's community-maintained
`CMakeLists.txt` reads it to determine the HarfBuzz version even though Sigil
does not invoke Meson. Release directories not used to build Sigil, such as
upstream tests, utilities, documentation, and language bindings, are omitted.

Sigil configures the upstream CMake project through
`3rdparty/cmake/harfbuzz.cmake`. That wrapper always builds static `harfbuzz`
and `harfbuzz-subset` targets and disables optional system integrations. Update
the source archive, checksum, version assertion, and documentation together
when upgrading HarfBuzz.
