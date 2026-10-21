// jxl-thumb — fast Windows Explorer thumbnails for JPEG XL.
//
// An IThumbnailProvider that decodes only what a thumbnail needs: in a lossy
// (VarDCT) JPEG XL the 1:8 "DC" image is stored before the detail, so for a
// 256 px thumbnail the decoder stops after the first few percent of the file.
// Lossless/modular files, small images and large requests fall back to a full
// decode. Pixels are converted to sRGB and box-filtered down to the requested
// size while they stream out of libjxl, so the full-resolution image is never
// held in memory.
//
// Registration is per user (HKCU\Software\Classes): no admin rights, and
// unregistering restores whatever handler Windows had before.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propsys.h>
#include <thumbcache.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include <jxl/cms.h>
#include <jxl/decode.h>
#include <jxl/thread_parallel_runner.h>

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// Bytes read from the file per step. The DC of a 24 MP master ends ~1.3 MB in,
// so small steps keep the read close to it...
static const ULONG READ_CHUNK = 256 * 1024;
// ...until this much has been read without reaching the DC: from there each
// step doubles, up to READ_CHUNK_MAX. Feeding libjxl a large file in small
// steps is slow (a 73 MB lossless file: 438 ms in 64 KB steps, 84 ms at once).
static const ULONG READ_GROW_AFTER = 2 * 1024 * 1024;
static const ULONG READ_CHUNK_MAX = 64 * 1024 * 1024;

// Upper bound on decoder threads per thumbnail. Explorer extracts several
// thumbnails at once, so all cores per file would only oversubscribe the CPU.
static const size_t MAX_THREADS = 8;

// Use the 1:8 DC image when its long side is at least this many times the
// requested size (1.0 = the DC is used whenever it has enough pixels).
static const double DC_MIN_RATIO = 1.0;

// ---------------------------------------------------------------------------

// {EF830030-FBD6-4B62-BFD4-0DBD590D6135}
static const CLSID CLSID_JxlThumb = {
    0xef830030, 0xfbd6, 0x4b62, {0xbf, 0xd4, 0x0d, 0xbd, 0x59, 0x0d, 0x61, 0x35}};
static const wchar_t kClsidStr[] = L"{EF830030-FBD6-4B62-BFD4-0DBD590D6135}";
static const wchar_t kThumbnailHandlerIid[] = L"{e357fccd-a995-4576-b01f-234630154e96}";
static const wchar_t kExtension[] = L".jxl";
static const wchar_t kDescription[] = L"jxl-thumb JPEG XL thumbnail provider";

static HINSTANCE g_module = nullptr;
static std::atomic<long> g_locks{0};

// ---------------------------------------------------------------------------
// Box-filter accumulator fed by libjxl's multithreaded image-out callback.
// One shared set of sums with a lock per destination row: a callback call
// delivers one row segment (a few hundred pixels), so the lock is cheap and
// memory stays at one accumulator however many threads the decoder runs.
// ---------------------------------------------------------------------------

struct Downscaler {
  size_t src_w = 0, src_h = 0;    // oriented image size, as the callback sees it
  size_t dst_w = 0, dst_h = 0;
  size_t channels = 4;            // 4 = RGBA, 2 = gray + alpha
  std::vector<uint32_t> xmap;     // source column -> destination column
  std::vector<uint32_t> ymap;     // source row -> destination row
  // Per destination pixel: sum(r*a), sum(g*a), sum(b*a), sum(a), count
  std::vector<uint64_t> sums;
  std::vector<SRWLOCK> row_locks;
  std::atomic<bool> wrote{false};

  void Setup(size_t sw, size_t sh, size_t dw, size_t dh) {
    src_w = sw; src_h = sh; dst_w = dw; dst_h = dh;
    xmap.resize(sw);
    ymap.resize(sh);
    for (size_t x = 0; x < sw; ++x) xmap[x] = static_cast<uint32_t>(x * dw / sw);
    for (size_t y = 0; y < sh; ++y) ymap[y] = static_cast<uint32_t>(y * dh / sh);
    row_locks.resize(dh);
    for (auto& l : row_locks) InitializeSRWLock(&l);
    sums.assign(dw * dh * 5, 0);
  }

  // libjxl may call this more than once per image (it did for lossless and
  // alpha images in 0.12), so it must not clear what earlier calls delivered.
  static void* Init(void* opaque, size_t, size_t) { return opaque; }

  static void Run(void* opaque, size_t, size_t x, size_t y, size_t n, const void* pixels) {
    auto* self = static_cast<Downscaler*>(opaque);
    if (y >= self->src_h || x >= self->src_w) return;
    const size_t dy = self->ymap[y];
    uint64_t* row = self->sums.data() + dy * self->dst_w * 5;
    const uint8_t* p = static_cast<const uint8_t*>(pixels);
    const size_t end = std::min(x + n, self->src_w);
    AcquireSRWLockExclusive(&self->row_locks[dy]);
    if (self->channels == 4) {
      for (size_t i = x; i < end; ++i, p += 4) {
        uint64_t* s = row + size_t(self->xmap[i]) * 5;
        const uint64_t a = p[3];
        s[0] += p[0] * a; s[1] += p[1] * a; s[2] += p[2] * a; s[3] += a; s[4] += 1;
      }
    } else {
      for (size_t i = x; i < end; ++i, p += 2) {
        uint64_t* s = row + size_t(self->xmap[i]) * 5;
        const uint64_t a = p[1];
        const uint64_t g = p[0] * a;
        s[0] += g; s[1] += g; s[2] += g; s[3] += a; s[4] += 1;
      }
    }
    ReleaseSRWLockExclusive(&self->row_locks[dy]);
    self->wrote = true;
  }

  static void Destroy(void*) {}

  // Premultiplied top-down BGRA, as a 32-bit DIB section wants it.
  HBITMAP ToBitmap() const {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(dst_w);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(dst_h);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return nullptr;
    uint8_t* out = static_cast<uint8_t*>(bits);
    const size_t n = dst_w * dst_h;
    for (size_t i = 0; i < n; ++i) {
      const uint64_t* t = sums.data() + i * 5;
      uint8_t* o = out + i * 4;
      if (t[4] == 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
      const uint64_t d = t[4] * 255;  // premultiplied: sum(c*a) / (count*255)
      o[0] = static_cast<uint8_t>((t[2] + d / 2) / d);
      o[1] = static_cast<uint8_t>((t[1] + d / 2) / d);
      o[2] = static_cast<uint8_t>((t[0] + d / 2) / d);
      o[3] = static_cast<uint8_t>((t[3] + t[4] / 2) / t[4]);
    }
    return bmp;
  }
};

// Decodes `stream` into a thumbnail whose long side is at most `cx`.
static HRESULT DecodeThumbnail(IStream* stream, UINT cx, HBITMAP* out_bmp,
                               WTS_ALPHATYPE* out_alpha) {
  if (cx == 0) return E_INVALIDARG;
  LARGE_INTEGER zero = {};
  stream->Seek(zero, STREAM_SEEK_SET, nullptr);

  JxlDecoder* dec = JxlDecoderCreate(nullptr);
  if (!dec) return E_OUTOFMEMORY;
  const size_t threads =
      std::min(MAX_THREADS, JxlThreadParallelRunnerDefaultNumWorkerThreads());
  void* runner = threads > 1 ? JxlThreadParallelRunnerCreate(nullptr, threads) : nullptr;

  struct Cleanup {
    JxlDecoder* dec; void* runner;
    ~Cleanup() {
      JxlDecoderDestroy(dec);
      if (runner) JxlThreadParallelRunnerDestroy(runner);
    }
  } cleanup{dec, runner};

  if (runner && JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner) !=
                    JXL_DEC_SUCCESS)
    return E_FAIL;
  if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |
                                         JXL_DEC_FRAME_PROGRESSION | JXL_DEC_FULL_IMAGE) !=
      JXL_DEC_SUCCESS)
    return E_FAIL;
  JxlDecoderSetProgressiveDetail(dec, kDC);
  JxlDecoderSetCms(dec, *JxlGetDefaultCms());
  JxlDecoderSetUnpremultiplyAlpha(dec, JXL_TRUE);

  Downscaler ds;
  JxlBasicInfo info = {};
  bool use_dc = false, have_info = false, have_output = false, input_set = false, eof = false;
  std::vector<uint8_t> buf;
  ULONG chunk = READ_CHUNK;
  unsigned long long total_read = 0;

  for (;;) {
    const JxlDecoderStatus st = JxlDecoderProcessInput(dec);
    if (st == JXL_DEC_ERROR) return E_FAIL;

    if (st == JXL_DEC_NEED_MORE_INPUT) {
      const size_t keep = input_set ? JxlDecoderReleaseInput(dec) : 0;
      if (keep) std::memmove(buf.data(), buf.data() + buf.size() - keep, keep);
      ULONG got = 0;
      if (!eof) {
        // Small steps only while the DC may be near; a full decode reads big.
        if (have_info && !use_dc) chunk = READ_CHUNK_MAX;
        else if (total_read >= READ_GROW_AFTER) chunk = std::min(chunk * 2, READ_CHUNK_MAX);
        buf.resize(keep + chunk);
        if (FAILED(stream->Read(buf.data() + keep, chunk, &got))) got = 0;
        if (got == 0) eof = true;
        total_read += got;
      }
      buf.resize(keep + got);
      if (got == 0) {
        // Truncated file: show whatever the decoder can render so far.
        if (have_output && JxlDecoderFlushImage(dec) == JXL_DEC_SUCCESS && ds.wrote) break;
        return E_FAIL;
      }
      JxlDecoderSetInput(dec, buf.data(), buf.size());
      input_set = true;
      continue;
    }

    if (st == JXL_DEC_BASIC_INFO) {
      if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) return E_FAIL;
      size_t w = info.xsize, h = info.ysize;
      if (info.orientation >= JXL_ORIENT_TRANSPOSE) std::swap(w, h);
      if (w == 0 || h == 0) return E_FAIL;
      const size_t long_side = std::max(w, h);
      const size_t target = std::min<size_t>(cx, long_side);
      size_t dw, dh;
      if (w >= h) { dw = target; dh = std::max<size_t>(1, (h * target + w / 2) / w); }
      else        { dh = target; dw = std::max<size_t>(1, (w * target + h / 2) / h); }
      ds.channels = info.num_color_channels == 1 ? 2 : 4;
      ds.Setup(w, h, dw, dh);
      use_dc = (long_side + 7) / 8 >= DC_MIN_RATIO * target && long_side > target;
      have_info = true;
      continue;
    }

    if (st == JXL_DEC_COLOR_ENCODING) {
      // sRGB, filled by hand: JxlColorEncodingSetToSRGB lives in the encoder
      // library and would pull it into the DLL.
      JxlColorEncoding srgb = {};
      srgb.color_space =
          info.num_color_channels == 1 ? JXL_COLOR_SPACE_GRAY : JXL_COLOR_SPACE_RGB;
      srgb.white_point = JXL_WHITE_POINT_D65;
      srgb.primaries = JXL_PRIMARIES_SRGB;
      srgb.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
      srgb.rendering_intent = JXL_RENDERING_INTENT_PERCEPTUAL;
      JxlDecoderSetOutputColorProfile(dec, &srgb, nullptr, 0);  // best effort
      continue;
    }

    if (st == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      JxlPixelFormat fmt = {static_cast<uint32_t>(ds.channels), JXL_TYPE_UINT8,
                            JXL_NATIVE_ENDIAN, 0};
      if (JxlDecoderSetMultithreadedImageOutCallback(dec, &fmt, Downscaler::Init,
                                                     Downscaler::Run, Downscaler::Destroy,
                                                     &ds) != JXL_DEC_SUCCESS)
        return E_FAIL;
      have_output = true;
      continue;
    }

    if (st == JXL_DEC_FRAME_PROGRESSION) {
      if (use_dc && JxlDecoderFlushImage(dec) == JXL_DEC_SUCCESS && ds.wrote) break;
      continue;
    }

    if (st == JXL_DEC_FULL_IMAGE) break;  // first frame of an animation is enough
    if (st == JXL_DEC_SUCCESS) {
      if (ds.wrote) break;
      return E_FAIL;
    }
  }

  if (!ds.wrote) return E_FAIL;
  HBITMAP bmp = ds.ToBitmap();
  if (!bmp) return E_OUTOFMEMORY;
  *out_bmp = bmp;
  *out_alpha = info.alpha_bits ? WTSAT_ARGB : WTSAT_RGB;
  return S_OK;
}

// ---------------------------------------------------------------------------
// COM objects
// ---------------------------------------------------------------------------

class JxlThumbProvider : public IThumbnailProvider, public IInitializeWithStream {
 public:
  JxlThumbProvider() { ++g_locks; }
  ~JxlThumbProvider() {
    if (stream_) stream_->Release();
    --g_locks;
  }

  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    static const QITAB qit[] = {
        QITABENT(JxlThumbProvider, IThumbnailProvider),
        QITABENT(JxlThumbProvider, IInitializeWithStream),
        {nullptr, 0},
    };
    return QISearch(this, qit, riid, ppv);
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  IFACEMETHODIMP_(ULONG) Release() override {
    const long r = --refs_;
    if (r == 0) delete this;
    return r;
  }

  IFACEMETHODIMP Initialize(IStream* stream, DWORD) override {
    if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    if (!stream) return E_INVALIDARG;
    stream_ = stream;
    stream_->AddRef();
    return S_OK;
  }

  IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
    if (!phbmp || !pdwAlpha) return E_POINTER;
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_UNKNOWN;
    if (!stream_) return E_UNEXPECTED;
    try {
      return DecodeThumbnail(stream_, cx, phbmp, pdwAlpha);
    } catch (const std::bad_alloc&) {
      return E_OUTOFMEMORY;
    } catch (...) {
      return E_FAIL;
    }
  }

 private:
  std::atomic<long> refs_{1};
  IStream* stream_ = nullptr;
};

class ClassFactory : public IClassFactory {
 public:
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    static const QITAB qit[] = {QITABENT(ClassFactory, IClassFactory), {nullptr, 0}};
    return QISearch(this, qit, riid, ppv);
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { ++g_locks; return 2; }
  IFACEMETHODIMP_(ULONG) Release() override { --g_locks; return 1; }

  IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* p = new (std::nothrow) JxlThumbProvider();
    if (!p) return E_OUTOFMEMORY;
    const HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
  }
  IFACEMETHODIMP LockServer(BOOL lock) override {
    if (lock) ++g_locks; else --g_locks;
    return S_OK;
  }
};

static ClassFactory g_factory;

// ---------------------------------------------------------------------------
// DLL exports
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (!IsEqualCLSID(clsid, CLSID_JxlThumb)) return CLASS_E_CLASSNOTAVAILABLE;
  return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_locks == 0 ? S_OK : S_FALSE; }

static LSTATUS SetString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                         const wchar_t* value) {
  HKEY key;
  LSTATUS rc = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                               KEY_SET_VALUE, nullptr, &key, nullptr);
  if (rc != ERROR_SUCCESS) return rc;
  rc = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                      static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return rc;
}

// Per-user registration: HKCU\Software\Classes overrides HKLM for this user only.
STDAPI DllRegisterServer() {
  wchar_t path[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(g_module, path, ARRAYSIZE(path));
  if (n == 0 || n >= ARRAYSIZE(path)) return HRESULT_FROM_WIN32(GetLastError());

  wchar_t clsid_key[128], inproc_key[160], ext_key[160];
  wsprintfW(clsid_key, L"Software\\Classes\\CLSID\\%s", kClsidStr);
  wsprintfW(inproc_key, L"%s\\InprocServer32", clsid_key);
  wsprintfW(ext_key, L"Software\\Classes\\%s\\ShellEx\\%s", kExtension, kThumbnailHandlerIid);

  LSTATUS rc = SetString(HKEY_CURRENT_USER, clsid_key, nullptr, kDescription);
  if (rc == ERROR_SUCCESS) rc = SetString(HKEY_CURRENT_USER, inproc_key, nullptr, path);
  if (rc == ERROR_SUCCESS)
    rc = SetString(HKEY_CURRENT_USER, inproc_key, L"ThreadingModel", L"Apartment");
  if (rc == ERROR_SUCCESS) rc = SetString(HKEY_CURRENT_USER, ext_key, nullptr, kClsidStr);
  if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);

  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}

// Deletes `subkey` only when it has neither subkeys nor values, so nothing
// another program stored there goes with it.
static void DeleteKeyIfEmpty(HKEY root, const wchar_t* subkey) {
  HKEY key;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
  DWORD subkeys = 1, values = 1;
  const LSTATUS rc = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr,
                                      nullptr, &values, nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(key);
  if (rc == ERROR_SUCCESS && subkeys == 0 && values == 0) RegDeleteKeyW(root, subkey);
}

STDAPI DllUnregisterServer() {
  wchar_t clsid_key[128], ext_key[160];
  wsprintfW(clsid_key, L"Software\\Classes\\CLSID\\%s", kClsidStr);
  wsprintfW(ext_key, L"Software\\Classes\\%s\\ShellEx\\%s", kExtension, kThumbnailHandlerIid);

  // Remove the .jxl handler entry only while it still points at us.
  wchar_t current[64] = {};
  DWORD size = sizeof(current);
  if (RegGetValueW(HKEY_CURRENT_USER, ext_key, nullptr, RRF_RT_REG_SZ, nullptr, current,
                   &size) == ERROR_SUCCESS &&
      _wcsicmp(current, kClsidStr) == 0)
    RegDeleteTreeW(HKEY_CURRENT_USER, ext_key);
  RegDeleteTreeW(HKEY_CURRENT_USER, clsid_key);

  // The keys registration created on the way, if nothing else uses them.
  wchar_t shellex_key[96], ext_root[64];
  wsprintfW(shellex_key, L"Software\\Classes\\%s\\ShellEx", kExtension);
  wsprintfW(ext_root, L"Software\\Classes\\%s", kExtension);
  DeleteKeyIfEmpty(HKEY_CURRENT_USER, shellex_key);
  DeleteKeyIfEmpty(HKEY_CURRENT_USER, ext_root);

  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}
