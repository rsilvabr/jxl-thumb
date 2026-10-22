# How it works

## The DC image of a lossy JPEG XL

A lossy JPEG XL (VarDCT mode, what `cjxl -d <distance>` produces for any
distance above 0) codes the image in 8 × 8 blocks, like JPEG. The average of
each block, the "DC" (in the spec: LF coefficients), forms a complete image at
1:8 scale. The codestream stores it **before** the detail (AC/HF):

```
[headers, ICC] [LF global] [LF groups ... ]  [HF global] [pass groups ............................]
 \_____________ the 1:8 image ____________/  \__________ the detail: 85-95 % of the file ___________/
```

libjxl can render a frame as soon as its LF groups are in:
`JxlDecoderSetProgressiveDetail(dec, kDC)` makes the decoder report
`JXL_DEC_FRAME_PROGRESSION` at that point, and `JxlDecoderFlushImage` turns
what it has into pixels. jxl-thumb stops there.

The first read takes a tenth of the file (at least 1 MB, at most 16 MB): on
`d=0.05` masters the DC ends 5–10 % in, so one read holds all of it and libjxl
decodes its groups in parallel. If the DC is not complete yet, each further
read doubles. Feeding the same data in small steps makes the decoder handle
the groups one at a time — on a 45 MP master that took 84 ms instead of 25 ms.

This does **not** need `cjxl --progressive_dc=1`. That option adds extra
levels *inside* the DC (1:16, 1:32, …) for very small previews; the 1:8 image
is always first in a VarDCT file. It was checked on files written by libjxl
0.12 with its default streaming encoder (`--buffering` left to cjxl), which is
how [jxl-photo](https://github.com/rsilvabr/jxl-photo) writes its masters.

## jxl-photo's thumbnail box (checked first)

Before any decode, jxl-thumb looks for the thumbnail
[jxl-photo](https://github.com/rsilvabr/jxl-photo) v2.16.0+ stores in the
file: an ISO BMFF `uuid` box (the format's way to carry a program's own data;
other readers skip it) with jxl-photo's UUID, whose payload is
`jxlphoto-thumbnail:v1:<bytes>:<hash>\n` followed by a JPEG (768 px, sRGB).
jxl-photo places it right before the codestream.

1. The container's boxes are walked with seeks: box headers, the thumbnail
   box, and the first 64 KiB of the codestream (`jxlc`, or the `jxlp` parts
   without their 4-byte index). A bare codestream has no boxes: straight to
   the decode.
2. The fingerprint is computed: codestream length + FNV-1a 64 of those
   64 KiB. It must equal the recorded one — an edit that re-encoded the pixels
   but kept the box leaves a stale value, and that thumbnail is ignored.
   Metadata edits do not touch the codestream and keep it valid.
3. The JPEG must be at least as large as the request (long side ≥ `cx`).
   Windows decodes it (WIC) and scales it with the Fant filter.

Any failure along the way falls back to the decode below. `kThumbBoxMax`
(16 MB) caps the box it will read. Why not EXIF: a 768 px JPEG does not fit
a JPEG's 64 KB EXIF segment, and djxl copies a JXL's EXIF into the JPEG it
writes — it would fail on such files.

## When the DC is not used

- **Lossless and modular files** (`-d 0`, `-m 1`) have no DC image; the
  decoder never reports the progression event and the file is decoded in full.
- **The DC is smaller than the thumbnail** — its long side is the image's
  long side ÷ 8. A 24 MP photo has a 756 px DC: plenty for Explorer's 256 px
  thumbnails, not enough for a 1024 px request. `DC_MIN_RATIO` at the top of
  `src/jxl_thumb.cpp` sets the rule.
- **The image is already small** (long side ≤ the requested size).

Explorer's preview pane has no JPEG XL preview handler to call (the
Microsoft extension registers none), so it asks the thumbnail provider for a
large image, the size of the pane. Those requests usually exceed the DC and
take the full-decode path.

In all these cases libjxl decodes the first frame completely, with up to
`MAX_THREADS` threads, reading up to 64 MB at a time.

## Threads

A trace of Explorer's thumbnail process showed it asking for one thumbnail at
a time, from one thread. So the time of a single thumbnail is what you wait
for, and each one gets up to 16 decoder threads (`MAX_THREADS`; fewer on a CPU
with fewer logical cores). On a 32-thread CPU, 16 threads made a 25–45 MP
thumbnail about 25 % faster than 8; more than 16 gained nothing. The
measurements are in [Performance notes](performance.md).

## Colour

Each image is converted to sRGB by libjxl's built-in CMS
(`JxlDecoderSetCms` + `JxlDecoderSetOutputColorProfile`). For XYB-encoded
lossy files the decoder produces sRGB directly from XYB; for lossless files
and ICC-profile images the CMS converts from the embedded profile. Explorer
draws thumbnails without colour management, so sRGB is what looks right on a
normal screen.

## From pixels to thumbnail

libjxl hands the pixels over row segment by row segment through
`JxlDecoderSetMultithreadedImageOutCallback`. Each pixel is added to the
destination pixel it falls in (a box filter), with alpha-weighted sums so
transparent areas do not darken the edges (images without alpha skip the
weighting).

When the DC is flushed, libjxl still renders it at full size, upsampled 8×.
That image holds no detail finer than the DC, so jxl-thumb sums only every 2nd
row and column of it (`DC_SAMPLE_STEP`): that takes a third off the render
step, and the result stays within about 0.6 dB of summing every pixel. A full
decode (lossless files, large requests) always sums every pixel. A lock per destination row keeps
the decoder's threads from colliding. When the decoder is done, the averages
become a premultiplied 32-bit bitmap — the format Explorer expects.

Because of that, memory use is the decoder's own working set plus one small
accumulator; no full-resolution buffer is ever allocated, even for the full
decode path.

## Registration

`DllRegisterServer` writes, for the current user only:

```
HKCU\Software\Classes\CLSID\{EF830030-FBD6-4B62-BFD4-0DBD590D6135}\InprocServer32
    (default) = <path to jxl_thumb.dll>
    ThreadingModel = Apartment
HKCU\Software\Classes\.jxl\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}
    (default) = {EF830030-FBD6-4B62-BFD4-0DBD590D6135}
```

`{e357fccd-…}` is the interface ID of `IThumbnailProvider`. Keys under
`HKCU\Software\Classes` take precedence over the machine-wide ones, so the
Microsoft extension's entry stays untouched under `HKLM` and comes back into
effect when `DllUnregisterServer` removes ours. Unregistering deletes the
`.jxl` entry only while it still points at jxl-thumb.

The provider implements `IInitializeWithStream`, which lets Explorer run it
in its isolated thumbnail process; it reads the file only through that
stream.
