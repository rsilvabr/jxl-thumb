@echo off
rem Builds build\jxl_thumb.dll and build\thumbtest.exe.
rem Needs Visual Studio 2022 Build Tools with the C++ workload. Downloads the
rem official libjxl static package once into deps\ and checks its SHA-256.
setlocal

rem --- settings -------------------------------------------------------------
set "LIBJXL_VERSION=0.12.0"
set "LIBJXL_ZIP=jxl-x64-windows-static.7z"
set "LIBJXL_SHA256=ff147dc7ac4ce55392974ccc70f2a8a8ec0eff3ae28529b072258b66c8f01ab2"
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0"
set "DEPS=%ROOT%deps"
set "JXL=%DEPS%\libjxl-%LIBJXL_VERSION%\x64-windows-static"
set "OUT=%ROOT%build"

if not exist "%JXL%\include\jxl\decode.h" (
    echo Downloading libjxl %LIBJXL_VERSION% static package...
    if not exist "%DEPS%" mkdir "%DEPS%"
    curl -L --fail -o "%DEPS%\%LIBJXL_ZIP%" "https://github.com/libjxl/libjxl/releases/download/v%LIBJXL_VERSION%/%LIBJXL_ZIP%" || goto :fail
    certutil -hashfile "%DEPS%\%LIBJXL_ZIP%" SHA256 | findstr /i /x "%LIBJXL_SHA256%" >nul || (
        echo SHA-256 mismatch for %LIBJXL_ZIP% - deleting it.
        del "%DEPS%\%LIBJXL_ZIP%"
        goto :fail
    )
    if not exist "%DEPS%\libjxl-%LIBJXL_VERSION%" mkdir "%DEPS%\libjxl-%LIBJXL_VERSION%"
    tar -xf "%DEPS%\%LIBJXL_ZIP%" -C "%DEPS%\libjxl-%LIBJXL_VERSION%" || goto :fail
)

rem The compiler environment: reuse an open Developer Prompt, else find vcvars64.
if defined VCINSTALLDIR goto :have_vc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vc
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :no_vc
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || goto :fail
:have_vc

if not exist "%OUT%" mkdir "%OUT%"
set "DEFINES=/DJXL_STATIC_DEFINE /DJXL_CMS_STATIC_DEFINE /DJXL_THREADS_STATIC_DEFINE /DHWY_STATIC_DEFINE /DUNICODE /D_UNICODE"
set "CFLAGS=/nologo /O2 /MT /EHsc /std:c++17 /W4 /utf-8 %DEFINES% /I"%JXL%\include""
set "JXLLIBS=jxl.lib jxl_cms.lib jxl_threads.lib hwy.lib brotlidec.lib brotlicommon.lib"
set "WINLIBS=ole32.lib shlwapi.lib shell32.lib advapi32.lib gdi32.lib user32.lib uuid.lib"

rc /nologo /fo "%OUT%\jxl_thumb.res" "%ROOT%src\jxl_thumb.rc" || goto :fail
cl %CFLAGS% /LD "%ROOT%src\jxl_thumb.cpp" "%OUT%\jxl_thumb.res" /Fo"%OUT%\\" /Fe"%OUT%\jxl_thumb.dll" ^
   /link /DEF:"%ROOT%src\jxl_thumb.def" /LIBPATH:"%JXL%\lib" %JXLLIBS% %WINLIBS% || goto :fail
cl %CFLAGS% "%ROOT%tools\thumbtest.cpp" /Fo"%OUT%\\" /Fe"%OUT%\thumbtest.exe" ^
   /link %WINLIBS% windowscodecs.lib || goto :fail

echo.
echo Built %OUT%\jxl_thumb.dll
exit /b 0

:no_vc
echo Visual Studio C++ Build Tools not found. See README, "Building from source".
:fail
echo BUILD FAILED
exit /b 1
