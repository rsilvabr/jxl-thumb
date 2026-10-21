# Version history

Detailed notes for every release, newest first. The [main README](../README.md)
keeps a short summary of the current release.

| Version | Date | Highlights |
|---------|------|------------|
| **[v0.1.0](#v010)** | 2026-10-21 | First version: Explorer thumbnails from the 1:8 DC image, sRGB colour, per-user install |

---

## v0.1.0

*2026-10-21*

First version.

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
