# Bundled HarfBuzz

Sigil vendors HarfBuzz so that font subsetting is available on every supported
platform without depending on a system HarfBuzz installation.

- Upstream version: 14.2.1
- Release date: 2026-06-02
- Source archive: https://github.com/harfbuzz/harfbuzz/releases/download/14.2.1/harfbuzz-14.2.1.tar.xz
- SHA-256: `a54a5d8e9380a41fbb762ce367bcbf7704792dfca0d93f1bbca86c5a57902e0e`
- License: MIT; see `COPYING`

The upstream `src` directory and build metadata are kept unchanged. This
includes `meson.build`, which remains the authoritative version metadata even
though Sigil does not invoke Meson. Release directories not used to build
Sigil, such as upstream tests, utilities, documentation, and language bindings,
are omitted.

Sigil's `3rdparty/cmake/harfbuzz.cmake` wrapper compiles the upstream-generated
`src/harfbuzz-subset.cc` amalgam as one static library. That supported simplified
build contains both the HarfBuzz core and subset APIs without creating two
static archives that repeat internal CFF and number implementations on MSVC.
Update the source archive, checksum, amalgam, version assertion, and
documentation together when upgrading HarfBuzz.
