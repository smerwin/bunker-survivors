# Third-party notices

This project is public domain (see LICENSE) except for the following, which keep their own licenses.

## mdx-m3-viewer

`client/lib/viewer.min.js` is a patched build of [mdx-m3-viewer](https://github.com/flowtsohg/mdx-m3-viewer) (the patch is applied by `scripts/patch-viewer.js`); it is also installed from npm.

```
The MIT License (MIT)

Copyright (c) 2013-2021 Chananya Freiman

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

```

## CascLib

`server/mndx.js` is a JavaScript port of `CascRootFile_MNDX.cpp` from [CascLib](https://github.com/ladislav-zezula/CascLib) by Ladislav Zezula, MIT license:

```
The MIT License (MIT)

Copyright (c) 2014 Ladislav Zezula

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
```

## Downloaded at run time (not included)

- **m3addon** `structures.xml` (GPL-2.0), fetched into `server/vendor/` on first use; only its data layout is read.
- **Epic Games PixelStreamingInfrastructure**, fetched into `Unreal/PixelStreaming/` by `Unreal/setup-pixelstreaming.ps1` under its own license.
- npm dependencies (`mdx-m3-viewer`, `qrcode`) under their own licenses.
