# Performance notes

What was measured while tuning v0.1.1, including what did not help. The code is
short; these numbers explain why it is written the way it is.

Setup: 16 real masters (4 each from a Nikon Zf 25 MP, D810 36 MP, Z7 and Z8
45 MP; ProPhoto RGB, `d=0.05 e=9`, 11–56 MB), 32-thread desktop CPU, files in
the disk cache, 256 px thumbnails unless stated. "Per photo" is the average
over the 16 files, decoded one after another. The bench loaded instrumented
builds of the DLL and timed each stage inside `DecodeThumbnail`.

## 1. Explorer asks for one thumbnail at a time

A build that logged every `GetThumbnail` call (process, thread, start, end,
calls in flight) was registered and a folder of fresh copies opened in
Explorer. All 25 calls came from **one thread of one `dllhost.exe`**, never
two at once, about 5 ms apart. Explorer asked for 96 px first (the folder's
default view) and again at 256 px after the view changed. Each size is a
separate decode.

So what you wait for is the time of a single thumbnail, and using several
cores for one file is the right lever. (v0.1.0 assumed Explorer ran several
extractions in parallel and capped each at 8 threads.)

## 2. Where the time went in v0.1.0

| Stage | Time per photo |
|---|---|
| Reading the file (cached) | 0.5 ms |
| Decoding the DC (`JxlDecoderProcessInput`) | 84 ms |
| Rendering it (`JxlDecoderFlushImage`, incl. the downscale callback) | 46–60 ms |
| Total | 130–150 ms |

Decoding the DC barely changed with the thread count:

| Threads | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---|---|---|---|---|---|
| DC decode | 105 ms | 87 ms | 87 ms | 84 ms | 84 ms | 85 ms |

## 3. Read the DC in one go

The file was fed to libjxl in 256 KB steps. With only part of the DC in its
buffer, libjxl decodes the DC groups that are complete, a few per call, so it
works through them almost serially. Giving it the whole DC at once lets it
decode all the groups in parallel:

| First read | DC decode | Reads per file |
|---|---|---|
| 256 KB steps | 84 ms | 7.2 |
| 1 MB | 38 ms | 2.2 |
| 2 MB | 29 ms | 1.4 |
| 4 MB | 25 ms | 1.0 |
| 16 MB | 25 ms | 1.0 |

The DC of these masters ends 0.8–3.7 MB into the file, at 4.8–10.3 % of its
size. A fixed size either over-reads small files or misses on large ones, so
the first read is a fraction of the file:

| First read | Read per file | Reads | Per photo |
|---|---|---|---|
| 4 MB fixed | 4.19 MB | 1.0 | 52.3 ms |
| file ÷ 8 | 3.91 MB | 1.0 | 52.3 ms |
| **file ÷ 10** | **3.12 MB** | **1.0** | **52.2 ms** |
| file ÷ 12 | 3.00 MB | 1.1 | 52.4 ms |
| file ÷ 16 | 3.16 MB | 1.4 | 55.0 ms |

v0.1.1 reads a tenth of the file (at least 1 MB, at most 16 MB), then doubles
each further read. Files at higher distances keep a larger share in the DC and
may need a second read. The 1 MB minimum means small files are read whole.

## 4. Threads per thumbnail

With the whole DC in one read, both stages scale:

| Threads | 2 | 4 | 8 | 16 | 32 |
|---|---|---|---|---|---|
| Per photo | 155 ms | 84 ms | 52 ms | **39 ms** | 54 ms |

At 32 threads, creating them costs 9 ms per thumbnail and the DC decode gets
slower again. v0.1.1 uses up to 16, or fewer on a CPU with fewer logical cores.

**Reusing one thread pool across thumbnails** (instead of creating threads for
each call) was tried. It saved about 3 ms at 16 threads (38.1 → 34.9 ms). It
was **not adopted**: threads that outlive a call keep running inside the DLL,
and Windows can unload a thumbnail DLL. Getting that right adds risk for little
gain.

## 5. Colour conversion is free

Asking libjxl for sRGB output instead of the file's own space made no
measurable difference (57.0 vs 59.0 ms of render, within noise). For XYB
(lossy) files the decoder produces sRGB directly.

## 6. The downscale

libjxl has no API to output the DC at its own 1:8 size. `JxlDecoderFlushImage`
renders it upsampled to full resolution, 45 million pixels for a Z7, and
every one went through the box filter. Replacing the filter with a callback
that does nothing cut the render step from 57 to 38.5 ms. About 19 ms per photo
was the downscale itself.

That upsampled image holds no detail finer than the DC, so not every pixel has
to be summed:

| Sum every… | Per photo | vs. a full decode (PSNR, 256 px) |
|---|---|---|
| pixel (v0.1.0) | 47.6 ms | 35–56 dB |
| **2nd row and column** | **39.7 ms** | **0.2–1.6 dB lower (about 0.6 on average)** |
| 4th row and column | 38.3 ms | 1–5 dB lower |

v0.1.1 sums every 2nd row and column. Step 4 saves another 1.5 ms but costs
1–5 dB. The reference is the same file decoded in full and box-filtered,
which shows what the sampling alone changes.

This applies only after the DC flush. Lossless files and large requests decode
in full and still sum every pixel, so their thumbnails are byte-identical to
v0.1.0's. Images without alpha also skip the alpha weighting, which saved
about 1 ms.

## 7. What is left

Per 45 MP photo, v0.1.1 spends about 15 ms decoding the DC and 16–18 ms in
libjxl's full-size render of it. The rest is thread setup and the bitmap.
Calling through the Windows shell adds about 15–20 ms per thumbnail: the
out-of-process call and the cache write. The render floor is set by libjxl;
an API to flush at 1:8 scale would remove most of it.

## Results

| | v0.1.0 | v0.1.1 |
|---|---|---|
| Per photo, `thumbtest direct` | 130–150 ms | 40 ms |
| 16 masters via the shell, 256 px | 2.2 s | 0.91 s |
| 16 masters via the shell, 1024 px | 3.6 s | 2.6 s |
| 16 MP lossless (full decode) | ~200 ms | 110 ms |
| Colour check vs. the TIFF in ImageMagick | 44.0 dB | 43.4 dB |

For comparison, the Microsoft JPEG XL extension 1.2.50 took 36.9 s for the
same 16 masters at any size.
