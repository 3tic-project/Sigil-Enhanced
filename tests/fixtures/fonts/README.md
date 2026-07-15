# Font subsetting test fixture

`OpenSans-Test.ttf` is a Latin-only subset of Open Sans Bold Build 100. It was
created from the EPUBCheck OpenType test fixture with HarfBuzz 14.1.0:

```sh
hb-subset OpenSans-Bold.ttf \
    --unicodes=U+0020,U+0041-005A,U+0061-007A \
    --output-file=OpenSans-Test.ttf
```

The source font metadata identifies Ascender Corporation and Google
Corporation and licenses the font under the Apache License, Version 2.0. A copy
of that license is already distributed in this repository at
`3rdparty/opencc/LICENSE`.

The fixture is intentionally small and contains only the glyph coverage needed
by the HarfBuzz integration and font-subsetting core tests.
