# jxl-thumb — fast JPEG XL thumbnails for Windows Explorer

A thumbnail provider for Windows Explorer that makes JPEG XL thumbnails from the
small preview image every lossy JXL already carries at the start of the file,
instead of decoding the whole photo. On 24–45 MP photo archives, thumbnails come
out about 15× faster than with the Microsoft JPEG XL extension, and only the
first few percent of each file is read.

**Current version: v0.1.0** (2026-10-21) · [All releases](docs/version_history.md) · [How it works](docs/how_it_works.md)

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

It only replaces the **thumbnail**. The preview pane, the Photos app and the
Properties dialog keep using whatever JPEG XL support Windows has.

## Measured

Twelve real photos (6 × 24 MP masters at `d=0.05`, 6 × 45 MP copies at
`d=0.5`, ProPhoto RGB), 256 px thumbnails requested through the Windows shell
on fresh copies (`thumbtest shell`), 32-thread desktop, files already in the
disk cache — so the time saved on reading from a hard disk is **not** in these
numbers:

| | Microsoft JPEG XL extension 1.2.50 | jxl-winthumb (WIC decoder) | **jxl-thumb** |
|---|---|---|---|
| All 12 photos | 22.9 s | 10.3 s | **1.46 s** |
| 24 MP master, per photo | 1.0–1.8 s | 0.38–0.83 s | **65–190 ms** |
| 45 MP photo, per photo | 2.2–2.6 s | 1.08–1.17 s | **120–155 ms** |
| Read from each file | all of it | all of it | **6.5 %** (24 MP), **12 %** (45 MP) |

Each column ran on its own fresh copy of the files, and the DLL Windows
actually loaded for it was checked in the thumbnail process. A first run of the Microsoft extension took 33 s, with two
photos at 5 and 8 s; the table shows the second, steadier run.

Other files (`thumbtest direct`, 256 px):

| File | Time | Read |
|---|---|---|
| 2048 px, `d=3` (a phone-size copy) | 29 ms | 47 % |
| JPEG transcoded to JXL (`cjxl photo.jpg`) | 30 ms | 15 % |
| 16 MP lossless (`d=0`, no DC → full decode) | 216 ms | 100 % |
| 24 MP lossy modular (`-m 1`, no DC → full decode) | 830 ms | 100 % |

Thumbnails match an independent reference (the original TIFF converted to
sRGB by ImageMagick) at 44 dB PSNR.

## Requirements

- Windows 10 or 11, 64-bit.
- Nothing else: libjxl is built into the DLL.
- It works alongside the Microsoft JPEG XL extension (or without it). With the
  extension installed, Windows still opens and previews JXLs with it; jxl-thumb
  only takes over the thumbnails.

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
- **Very large thumbnail sizes** (Explorer's "Extra large icons" on a high-DPI
  screen can ask for 768–1024 px) may be bigger than the DC of a 24 MP photo;
  those are decoded in full.
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

Settings (read size per step, thread cap, when to use the DC) are constants at
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
