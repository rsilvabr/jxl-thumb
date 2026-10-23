@echo off
rem Builds and packs dist\jxl-thumb-<version>-x64.zip (the GitHub release asset).
setlocal
set "ROOT=%~dp0"
rem JXLTHUMB_SKIP_BUILD=1 packs what build\ already holds (the release workflow
rem builds, has the DLL signed, then packs).
if not defined JXLTHUMB_SKIP_BUILD call "%ROOT%build.cmd" || exit /b 1

for /f "tokens=3" %%v in ('findstr /c:"JXLTHUMB_VERSION_STR" "%ROOT%src\version.h"') do set "VER=%%~v"
set "NAME=jxl-thumb-%VER%-x64"
set "STAGE=%ROOT%dist\%NAME%"

if exist "%STAGE%" rd /s /q "%STAGE%"
mkdir "%STAGE%\licenses" || exit /b 1
copy /y "%ROOT%build\jxl_thumb.dll" "%STAGE%\" >nul
copy /y "%ROOT%build\thumbtest.exe" "%STAGE%\" >nul
copy /y "%ROOT%install.cmd" "%STAGE%\" >nul
copy /y "%ROOT%uninstall.cmd" "%STAGE%\" >nul
copy /y "%ROOT%clear_thumbnail_cache.cmd" "%STAGE%\" >nul
copy /y "%ROOT%README.md" "%STAGE%\" >nul
copy /y "%ROOT%LICENSE" "%STAGE%\" >nul
copy /y "%ROOT%licenses\*" "%STAGE%\licenses\" >nul

if exist "%ROOT%dist\%NAME%.zip" del "%ROOT%dist\%NAME%.zip"
tar -a -c -f "%ROOT%dist\%NAME%.zip" -C "%ROOT%dist" "%NAME%" || exit /b 1
echo Packed dist\%NAME%.zip
