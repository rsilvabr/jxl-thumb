// thumbtest — times and saves JPEG XL thumbnails.
//
//   thumbtest direct <jxl_thumb.dll> <size> <out_dir|-> <file.jxl>...
//       Loads the DLL straight from disk (nothing registered) and calls its
//       IThumbnailProvider, like Explorer's thumbnail process does.
//   thumbtest shell <size> <out_dir|-> <file.jxl>...
//       Asks the Windows shell (IShellItemImageFactory) for the thumbnail, so
//       whatever handler is registered for .jxl does the work. Use fresh copies
//       of the files: the shell answers from its cache when it can.
//
// Prints one line per file: size, milliseconds and, in direct mode, how many
// bytes of the file the provider read.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <wincodec.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <string>

#include "../src/version.h"

typedef HRESULT(STDAPICALLTYPE* GetClassObjectFn)(REFCLSID, REFIID, void**);

static const CLSID CLSID_JxlThumb = {
    0xef830030, 0xfbd6, 0x4b62, {0xbf, 0xd4, 0x0d, 0xbd, 0x59, 0x0d, 0x61, 0x35}};

static double NowMs() {
  static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return 1000.0 * double(t.QuadPart) / double(freq.QuadPart);
}

static HRESULT SavePng(HBITMAP bmp, WTS_ALPHATYPE alpha, const std::wstring& path) {
  IWICImagingFactory* wic = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic));
  if (FAILED(hr)) return hr;
  IWICBitmap* src = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* enc = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  hr = wic->CreateBitmapFromHBITMAP(
      bmp, nullptr, alpha == WTSAT_ARGB ? WICBitmapUsePremultipliedAlpha : WICBitmapIgnoreAlpha,
      &src);
  if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
  if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
  if (SUCCEEDED(hr)) hr = frame->WriteSource(src, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = enc->Commit();
  if (frame) frame->Release();
  if (enc) enc->Release();
  if (stream) stream->Release();
  if (src) src->Release();
  wic->Release();
  return hr;
}

static void Report(const wchar_t* file, HRESULT hr, HBITMAP bmp, WTS_ALPHATYPE alpha, double ms,
                   long long bytes_read, long long file_size, const wchar_t* out_dir,
                   const wchar_t* tag) {
  if (FAILED(hr) || !bmp) {
    wprintf(L"FAIL  0x%08lx  %s\n", static_cast<unsigned long>(hr), file);
    return;
  }
  BITMAP bm = {};
  GetObjectW(bmp, sizeof(bm), &bm);
  wprintf(L"%4ldx%-4ld %8.1f ms", bm.bmWidth, bm.bmHeight, ms);
  if (bytes_read >= 0)
    wprintf(L"  read %7.2f of %7.2f MB (%5.1f%%)", bytes_read / 1e6, file_size / 1e6,
            file_size ? 100.0 * bytes_read / file_size : 0.0);
  wprintf(L"  %s\n", PathFindFileNameW(file));
  if (wcscmp(out_dir, L"-") != 0) {
    std::wstring name = PathFindFileNameW(file);
    PathRemoveExtensionW(&name[0]);
    name.resize(wcslen(name.c_str()));
    std::wstring out = std::wstring(out_dir) + L"\\" + name + L"_" + tag + L".png";
    const HRESULT shr = SavePng(bmp, alpha, out);
    if (FAILED(shr)) wprintf(L"      (could not save PNG: 0x%08lx)\n", static_cast<unsigned long>(shr));
  }
}

static int Direct(int argc, wchar_t** argv) {
  const wchar_t* dll = argv[2];
  const UINT size = static_cast<UINT>(_wtoi(argv[3]));
  const wchar_t* out_dir = argv[4];
  HMODULE mod = LoadLibraryW(dll);
  if (!mod) { wprintf(L"cannot load %s (error %lu)\n", dll, GetLastError()); return 2; }
  auto get = reinterpret_cast<GetClassObjectFn>(GetProcAddress(mod, "DllGetClassObject"));
  IClassFactory* cf = nullptr;
  if (!get || FAILED(get(CLSID_JxlThumb, IID_PPV_ARGS(&cf)))) {
    wprintf(L"DllGetClassObject failed\n");
    return 2;
  }
  int failures = 0;
  wchar_t tag[32];
  swprintf(tag, 32, L"direct%u", size);
  for (int i = 5; i < argc; ++i) {
    IStream* stream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(argv[i], STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE,
                                        nullptr, &stream);
    if (FAILED(hr)) { Report(argv[i], hr, nullptr, WTSAT_UNKNOWN, 0, -1, 0, out_dir, tag); ++failures; continue; }
    STATSTG st = {};
    stream->Stat(&st, STATFLAG_NONAME);
    IInitializeWithStream* init = nullptr;
    IThumbnailProvider* prov = nullptr;
    HBITMAP bmp = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    const double t0 = NowMs();
    hr = cf->CreateInstance(nullptr, IID_PPV_ARGS(&init));
    if (SUCCEEDED(hr)) hr = init->Initialize(stream, STGM_READ);
    if (SUCCEEDED(hr)) hr = init->QueryInterface(IID_PPV_ARGS(&prov));
    if (SUCCEEDED(hr)) hr = prov->GetThumbnail(size, &bmp, &alpha);
    const double ms = NowMs() - t0;
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER pos = {};
    stream->Seek(zero, STREAM_SEEK_CUR, &pos);
    Report(argv[i], hr, bmp, alpha, ms, static_cast<long long>(pos.QuadPart),
           static_cast<long long>(st.cbSize.QuadPart), out_dir, tag);
    if (FAILED(hr)) ++failures;
    if (bmp) DeleteObject(bmp);
    if (prov) prov->Release();
    if (init) init->Release();
    stream->Release();
  }
  cf->Release();
  return failures ? 1 : 0;
}

static int Shell(int argc, wchar_t** argv) {
  const UINT size = static_cast<UINT>(_wtoi(argv[2]));
  const wchar_t* out_dir = argv[3];
  int failures = 0;
  wchar_t tag[32];
  swprintf(tag, 32, L"shell%u", size);
  for (int i = 4; i < argc; ++i) {
    wchar_t full[32768];
    GetFullPathNameW(argv[i], ARRAYSIZE(full), full, nullptr);
    IShellItemImageFactory* f = nullptr;
    HBITMAP bmp = nullptr;
    const double t0 = NowMs();
    HRESULT hr = SHCreateItemFromParsingName(full, nullptr, IID_PPV_ARGS(&f));
    if (SUCCEEDED(hr)) {
      SIZE sz = {static_cast<LONG>(size), static_cast<LONG>(size)};
      hr = f->GetImage(sz, SIIGBF_THUMBNAILONLY, &bmp);
    }
    const double ms = NowMs() - t0;
    Report(full, hr, bmp, WTSAT_ARGB, ms, -1, 0, out_dir, tag);
    if (FAILED(hr)) ++failures;
    if (bmp) DeleteObject(bmp);
    if (f) f->Release();
  }
  return failures ? 1 : 0;
}

int wmain(int argc, wchar_t** argv) {
  _setmode(_fileno(stdout), _O_U8TEXT);  // Japanese file names print intact
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int rc;
  if (argc >= 6 && wcscmp(argv[1], L"direct") == 0) {
    rc = Direct(argc, argv);
  } else if (argc >= 5 && wcscmp(argv[1], L"shell") == 0) {
    rc = Shell(argc, argv);
  } else {
    wprintf(L"thumbtest %hs\n"
            L"  thumbtest direct <jxl_thumb.dll> <size> <out_dir|-> <file.jxl>...\n"
            L"  thumbtest shell <size> <out_dir|-> <file.jxl>...\n",
            JXLTHUMB_VERSION_STR);
    rc = 2;
  }
  CoUninitialize();
  return rc;
}
