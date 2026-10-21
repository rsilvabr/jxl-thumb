# jxl-thumb — fast JPEG XL thumbnails for Windows Explorer

A thumbnail provider for Windows Explorer that makes JPEG XL thumbnails from the
small preview image every lossy JXL already carries at the start of the file,
instead of decoding the whole photo. On 25–45 MP photo archives, thumbnails come
out about 40× faster than with the Microsoft JPEG XL extension, in the right
colours for wide-gamut (ProPhoto, Adobe RGB, P3) files, and only about a tenth
of each file is read.

**Current version: v0.1.1** (2026-10-22) · [All releases](docs/version_history.md) · [How it works](docs/how_it_works.md)

---

## Contents

- [Why](#why)
- [What it does](#what-it-does)
- [Measured](#measured)
- [Requirements](#requirements)
- [Install](#install)
- [Uninstall](#uninstall)
- [Refreshing thumbnails Windows already made](#refreshing-thumbnails-windows-already-made)
- [Good to know](#good-to-know)
- [Building from source](#building-from-source)
- [Testing on your own files](#testing-on-your-own-files)
- [Related project](#related-project)
- [License](#license) · [Acknowledgments](#acknowledgments)

---

## Why

With the Microsoft JPEG XL extension — or a third-party WIC decoder such as
jxl-winthumb — Explorer decodes every photo at full resolution just to draw a
256-pixel thumbnail. A folder of 45 MP archives fills
slowly, and on a hard disk every file is read from the first byte to the last.

JPEG XL does not need that. A lossy (VarDCT) JPEG XL stores a 1:8 version of the
image — the "DC" — before the fine detail. For a 6048 × 4032 photo that is a
756 × 504 image, already more than a thumbnail needs, and it sits in the first
5–15 % of the file. jxl-thumb asks libjxl for that image and stops.

## What it does

- **Reads only the start of the file** for lossy JXLs, then stops: no detail
  decoding, no reading the rest from disk.
- **Works on the files you already have.** The DC is always stored first in a
  VarDCT JXL; no re-encode and no special `cjxl` option (such as
  `--progressive_dc`) is needed.
- **Uses the cores** — Explorer asks for one thumbnail at a time, so each one
  is decoded with up to 16 threads.
- **Falls back to a full decode** (multi-threaded libjxl) when the DC is not
  enough: lossless/modular files, small images, or a thumbnail larger than the
  DC.
- **Correct colours** — every image is converted to sRGB, so ProPhoto, Adobe RGB
  and Display P3 masters look right.
- **Orientation, alpha, grayscale, animation** (first frame) and **truncated
  files** (whatever part decodes) are handled.
- **Low memory** — pixels are averaged down to the thumbnail size while they
  come out of the decoder; the full-size image is never held in memory.
- **Per-user install, no admin rights.** Uninstalling gives Explorer back the
  handler it had before.

**The preview pane too.** No JPEG XL preview handler is registered by the
Microsoft extension, so Explorer's preview pane shows the thumbnail at a large
size — which now comes from jxl-thumb, fast and with the right colours. The
Photos app and other programs keep using whatever JPEG XL support they have.

## Measured

Sixteen real photo masters — 4 each from a Nikon Zf (25 MP), D810 (36 MP),
Z7 and Z8 (45 MP), exported in ProPhoto RGB and encoded at `d=0.05` (11–56 MB
each) — requested through the Windows shell on fresh copies
(`thumbtest shell`), 32-thread desktop, files already in the disk cache (so
the time saved on reading from a hard disk is **not** in these numbers):

| Per photo (average) | Microsoft JPEG XL extension 1.2.50 | **jxl-thumb 0.1.1** |
|---|---|---|
| Zf, 25 MP | 1.08 s | **36 ms** |
| D810, 36 MP | 2.09 s | **66 ms** |
| Z7, 45 MP | 3.17 s | **62 ms** |
| Z8, 45 MP | 2.86 s | **56 ms** |
| **All 16, 256 px thumbnails** | **36.9 s** | **0.91 s** |
| All 16, 1024 px (preview-pane size) | 36.9 s | 2.6 s |
| Read from each file | all of it | about 10 % at 256 px |

At 1024 px the DC of a 25–36 MP photo is too small, so jxl-thumb decodes the
whole photo — still about 14× faster than the extension, which takes the same
time at any size because it always decodes everything. The DLL Windows loaded
for each run was checked in the thumbnail process. (v0.1.0, measured the same
way: 2.2 s and 3.6 s.)

An earlier run on 12 other photos (24 MP at `d=0.05`, 45 MP at `d=0.5`) also
measured jxl-winthumb, a third-party WIC decoder: 22.9 s (Microsoft), 10.3 s
(jxl-winthumb), 1.46 s (jxl-thumb).

Other files (`thumbtest direct`, 256 px):

| File | Time | Read |
|---|---|---|
| 2048 px, `d=3` (a phone-size copy) | 23 ms | 100 % (0.56 MB) |
| JPEG transcoded to JXL (`cjxl photo.jpg`) | 23 ms | 61 % (1 MB) |
| 16 MP lossless (`d=0`, no DC → full decode) | 110 ms | 100 % |
| 24 MP lossy modular (`-m 1`, no DC → full decode) | 560 ms | 100 % |

The first read is a tenth of the file and at least 1 MB, so small files are
read whole in one go.

Thumbnails match an independent reference (the original TIFF converted to
sRGB by ImageMagick) at 43 dB PSNR.

## Requirements

- Windows 10 or 11, 64-bit.
- Nothing else: libjxl is built into the DLL.
- It works alongside the Microsoft JPEG XL extension (or without it). With the
  extension installed, Windows still opens JXLs with it; jxl-thumb only takes
  over the thumbnails (and with them the preview pane).

## Install

1. Download `jxl-thumb-<version>-x64.zip` from
   [Releases](https://github.com/rsilvabr/jxl-thumb/releases) and extract it
   anywhere.
2. Double-click **`install.cmd`**.

That copies `jxl_thumb.dll` to `%LOCALAPPDATA%\jxl-thumb` and registers it for
your Windows user only (`HKEY_CURRENT_USER`), so no administrator rights are
needed and other users of the PC are not affected. New thumbnails use it at
once; see the next sections for the ones Windows already made.

To update, extract the new version and run `install.cmd` again. Explorer may
still hold the old DLL open; the installer renames it out of the way and
removes it on a later run.

## Uninstall

Double-click **`uninstall.cmd`**. It unregisters the DLL and deletes
`%LOCALAPPDATA%\jxl-thumb`. Explorer goes back to the thumbnail handler it had
before (the Microsoft extension, if installed). If Explorer still has the DLL
open, the folder is deleted after the next sign-out.

## Refreshing thumbnails Windows already made

Windows keeps thumbnails in a cache (`thumbcache_*.db`) and does not ask any
handler again for a photo it has already seen. After installing, folders you
had opened before keep their old thumbnails until the cache is cleared:

- **`clear_thumbnail_cache.cmd`** — closes Explorer for a moment, deletes the
  cache and starts Explorer again; or
- **Disk Cleanup** → tick **Thumbnails** → OK.

Either way *all* thumbnails (not only JXL) are rebuilt as you browse.

## Good to know

- **Windows clears the thumbnail cache by itself.** Storage Sense / automatic
  Disk Cleanup deletes it now and then, which is why big folders sometimes look
  "new" again. To stop that, set `Autorun` to `0` in
  `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VolumeCaches\Thumbnail Cache`
  (admin rights needed).
- **Lossless JXLs** have no DC image, so they are decoded in full — still with
  libjxl's multi-threaded decoder, usually much faster than the Microsoft
  extension, but they read the whole file.
- **JPEGs transcoded losslessly to JXL** (`cjxl photo.jpg`) are VarDCT too and
  get the fast path.
- **HDR images** (PQ/HLG) are converted to sRGB without tone mapping: their
  thumbnails can look flat or clipped.
- **The preview pane and very large icons** ask for big images (often
  768–1600 px), larger than the DC of most photos; those are decoded in full —
  about 0.2 s for a 45 MP photo.
- Explorer runs thumbnail handlers in a separate process (`dllhost.exe`), so a
  damaged file cannot crash Explorer itself.

## Building from source

1. Install **Visual Studio 2026 Build Tools** with the C++ workload:
   ```
   winget install --id Microsoft.VisualStudio.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
   The official libjxl Windows package is built with the 2026 toolset; Visual
   Studio 2022 fails to link it (`__std_min_element_1u` unresolved).
2. Run **`build.cmd`**. The first run downloads the official
   [libjxl 0.12.0](https://github.com/libjxl/libjxl/releases/tag/v0.12.0)
   static package into `deps\` and checks its SHA-256.
3. Output: `build\jxl_thumb.dll` and `build\thumbtest.exe`. `install.cmd` picks
   the DLL up from `build\` when there is none next to it.
4. `package.cmd` builds and packs the release zip into `dist\`.

Settings (read sizes, thread cap, when to use the DC) are constants at
the top of [`src/jxl_thumb.cpp`](src/jxl_thumb.cpp).

## Testing on your own files

`thumbtest.exe` makes thumbnails and prints how long each took and how much of
the file was read:

```
build\thumbtest.exe direct build\jxl_thumb.dll 256 out_folder D:\photos\*.jxl
build\thumbtest.exe shell 256 - D:\photos\copy\*.jxl
```

- `direct` loads the DLL straight from disk — nothing needs to be installed.
- `shell` asks Windows for the thumbnail, so it measures whichever handler is
  registered (run it before and after installing to compare). Use fresh copies
  of the files: Windows answers from its cache for files it has seen.
- `out_folder` receives a PNG per file; `-` saves nothing.

## Related project

[jxl-photo](https://github.com/rsilvabr/jxl-photo) — batch TIFF/JPEG ↔ JPEG XL
conversion for photo archives, with ICC and metadata preservation. jxl-thumb
was written for the archives it makes, but works with any JPEG XL file.

## License

MIT — see [LICENSE](LICENSE). The DLL includes
[libjxl](https://github.com/libjxl/libjxl) (BSD-3-Clause),
[Brotli](https://github.com/google/brotli) (MIT),
[Highway](https://github.com/google/highway) (Apache-2.0) and
[skcms](https://skia.googlesource.com/skcms) (BSD-3-Clause); their licenses are
in [`licenses/`](licenses/).

## Acknowledgments

- [libjxl](https://github.com/libjxl/libjxl) team for JPEG XL and a decoder API
  that makes "stop after the DC" a few lines of code
- [Claude](https://www.anthropic.com/claude) (Anthropic), among other AI tools,
  for code assistance, reviews and technical discussion
