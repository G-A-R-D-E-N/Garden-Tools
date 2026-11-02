# Third party notices

Radicle's own source is covered by the repository licence. Software written by others and shipped
alongside it, or ported into it, is listed below with its own licence. Fallout 4 and its file formats
are Bethesda's; nothing here is derived from Bethesda's code or ships their assets.

## bcdec

- Upstream: https://github.com/iOrange/bcdec
- Licence: MIT / Unlicense (dual), by Sergii Kudlai. The full text of both options is below.

`src/texture/bcn.cpp` is the bcdec BC1-BC7 block decoder, ported from bcdec.h v0.97. It is the same
decoder Godot ships as `modules/bcdec` and the same one PluginEditTool carries at
`PluginEditTool/native/src/textures/BcnDecoder.cpp`; the code moved here unchanged apart from its
namespace, its two public entry points (`can_decode_block_format` and `decode_block_format`) and a
thrown error instead of a returned one. Two things are carried across verbatim because they are fixed
spec data that cannot be derived: the 64 two-subset and 64 three-subset BC7 partition tables, and the
per-mode endpoint bit widths. BC6H (HDR) is not ported.

The port was checked against DirectXTex's own conversion of the same files, pixel for pixel, over 484
real mod textures covering every format: BC2, BC3, BC5, BC7 and the uncompressed layouts match
exactly, and BC1 and BC4 differ by at most 1 in the rounding of a rare interpolated value. That
measurement was made for the PluginEditTool port; `tests/test_dds.cpp` pins the formats it covers.

PluginEditTool still holds its own copy. Pointing it at `fo4::texture` is a follow-up and is not part
of the change that created this file.

`src/texture/dds_codec.cpp`, `src/texture/dds.cpp` and `src/texture/bcn.cpp` also carry Radicle's own
DDS container reader and one-mip RGBA8 decode. Nothing there comes from bcdec.

### bcdec licence text

This software is available under 2 licenses -- choose whichever you prefer.

------------------------------------------------------------------------------
ALTERNATIVE A - MIT License

Copyright (c) 2022 Sergii Kudlai

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

------------------------------------------------------------------------------
ALTERNATIVE B - The Unlicense

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>

## nlohmann/json

- Upstream: https://github.com/nlohmann/json
- Licence: MIT. Copyright (c) 2013 - 2025 Niels Lohmann, version 3.12.0.

Radicle fetches the v3.12.0 release asset `json.tar.xz` at configure time (`Radicle/CMakeLists.txt:nlohmann_json`), pinned by SHA256, and includes `<nlohmann/json.hpp>` from `Radicle/include/fo4/graph/graph_document.hpp`. Nothing is vendored in this tree. Object code built from the header carries parts of it, so the licence text is reproduced below.

### nlohmann/json licence text

Copyright (c) 2013 - 2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## NifSkope Wild Wasteland Edition

- Upstream: https://github.com/BungoV/NifSkope-WildWastelandEdition
- Licence: BSD-3, as stated in the fork's README and `LICENSE.md`. BungoV told Nomad the work can be used.

`src/nif/hknp_packfile.cpp` and `src/nif/hknp_decode_*.cpp` are adapted from the fork's `hknpdecode.cpp` and `hknpdecode.h` at commit `5422be6c`. The fork's GPLv3 `nif.xml` and the third-party libraries it lists are not included.

`src/nif/hknp_encode.cpp` encoder layouts were cross-checked from the same source and re-measured on vanilla.

```
Copyright (c) 2005-2014, NIF File Format Library and Tools.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## modkit21, bacup and related repositories

The AS3, LOD, LAND, FUZE and XWMA, cloth profile, voice path, pitch shift, pchip and palette, animation text table and NIF audit code under `Radicle/src` and `Radicle/include/fo4` was written from study of the upstream reference repositories, which are licensed GPL-3.0.
