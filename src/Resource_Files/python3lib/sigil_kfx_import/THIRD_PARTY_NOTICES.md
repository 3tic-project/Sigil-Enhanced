# KFX import worker third-party notices

## KFX Input / kfxlib conversion core

- Upstream-derived source: <https://github.com/2778995958/kfx2epub>
- Imported baseline: `a5530a923bf74dd53af5a64c9f50aecbbff90ef6`
- Original KFX Input/kfxlib author: John Howell, 2016–2025
- License declared by the imported source files: GNU General Public License v3
- Local changes: packaged below `sigil_kfx_import`, invoked by a new JSON Lines worker, and integrated with Sigil-Enhanced's input/output safety gates.

The complete GNU GPL version 3 text is distributed as the repository and
application `COPYING.txt`.

## Beautiful Soup 4.13.4 and Soup Sieve 2.7

The isolated KFX runtime uses Beautiful Soup 4.13.4 and its Soup Sieve 2.7
dependency for `lxml.html.soupparser` fallback parsing. Both are distributed
under the MIT License. Their package metadata and license files are preserved
in the synchronized application `python3lib` tree.

## typing_extensions 4.13.2

The bundled `kfxlib/calibre-plugin-modules/typing_extensions.py` module is
typing_extensions 4.13.2. It is distributed under the Python Software
Foundation License 2.0 (`PSF-2.0`). The license text is available from the
[typing_extensions source repository](https://github.com/python/typing_extensions/blob/4.13.2/LICENSE).

## pypdf 5.9.0

The bundled `kfxlib/calibre-plugin-modules/pypdf` package is pypdf 5.9.0.
It is distributed under the BSD 3-Clause License:

```text
Copyright (c) 2006-2008, Mathieu Fenniak
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
