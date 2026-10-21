# Version history

Detailed notes for every release, newest first. The [main README](../README.md)
keeps a short summary of the current release.

| Version | Date | Highlights |
|---------|------|------------|
| **[v0.1.1](#v011)** | 2026-10-22 | About 2.4× faster: the DC read in one go, up to 16 threads, lighter downscale |
| [v0.1.0](#v010) | 2026-10-21 | First version: Explorer thumbnails from the 1:8 DC image, sRGB colour, per-user install |

---

## v0.1.1

*2026-10-22*

Faster thumbnails; nothing to change on your side. On the same 16 masters,
through the Windows shell: 0.91 s instead of 2.2 s at 256 px, and 2.6 s instead
of 3.6 s at 1024 px (preview-pane size).

- **The DC is read in one go.** The first read is now a tenth of the file
  (1–16 MB) instead of 256 KB steps. With all of the DC at hand libjxl decodes
  its groups in parallel: 25 ms instead of 84 ms on a 45 MP master. Reads after
  that double in size.
- **Up to 16 threads per thumbnail** (was 8). Explorer asks for one thumbnail
  at a time — checked with a trace of its thumbnail process — so a single
  thumbnail can use the cores. Above 16 nothing was gained.
- **Lighter downscale for the DC.** libjxl renders the DC at full size; only
  every 2nd row and column of it is summed now, and images without alpha skip
  the alpha weighting. Full decodes still sum every pixel: lossless thumbnails
  are byte-identical to v0.1.0's.
- Lossless and modular files gain from the threads too (16 MP lossless: 110 ms,
  was ~200 ms).

---

## v0.1.0

*2026-10-21*

First version. On 16 real 25–45 MP masters (`d=0.05`), thumbnails took
2.6 s against 36.9 s with the Microsoft JPEG XL extension 1.2.50 (README,
"Measured"). Explorer's preview pane uses the same provider, so it gets
faster and colour-correct too.

- **Thumbnails from the DC image.** For lossy (VarDCT) JPEG XL files the
  thumbnail is built from the 1:8 image stored at the start of the codestream;
  the rest of the file is never read. Existing files work as they are: no
  re-encode, no `--progressive_dc` needed.
- **Full decode only when needed** — lossless/modular files, images whose DC
  is smaller than the requested size, and the first frame of an animation.
- **Colour** — every image is converted to sRGB by libjxl (its built-in CMS),
  so wide-gamut masters (ProPhoto, Adobe RGB, Display P3) show correct colours.
- **Orientation** from the codestream is applied; alpha and grayscale images
  are supported; a truncated file shows whatever part can be decoded.
- **Low memory** — pixels are box-filtered to the thumbnail size while they
  stream out of the decoder, so the full-resolution image is never held in
  memory.
- **Per-user install** (`install.cmd` / `uninstall.cmd`), no admin rights;
  uninstalling restores the previous thumbnail handler.
- `thumbtest.exe` times the provider on any set of files, directly or through
  the Windows shell.
- Built against libjxl 0.12.0 (official static package, SHA-256 checked).
