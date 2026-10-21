@echo off
rem Installs jxl-thumb for the current user: copies jxl_thumb.dll to
rem %LOCALAPPDATA%\jxl-thumb and registers it under HKCU. No admin rights.
setlocal

set "SRC=%~dp0jxl_thumb.dll"
if not exist "%SRC%" set "SRC=%~dp0build\jxl_thumb.dll"
if not exist "%SRC%" goto :no_dll
set "DEST=%LOCALAPPDATA%\jxl-thumb"
if not exist "%DEST%" mkdir "%DEST%"

rem Explorer may still hold the old DLL: a loaded DLL cannot be overwritten
rem but can be renamed. Leftovers from earlier updates are removed when free.
del /q "%DEST%\*.old" >nul 2>&1
if exist "%DEST%\jxl_thumb.dll" move /y "%DEST%\jxl_thumb.dll" "%DEST%\jxl_thumb.dll.%RANDOM%.old" >nul
copy /y "%SRC%" "%DEST%\jxl_thumb.dll" >nul || goto :fail
regsvr32 /s "%DEST%\jxl_thumb.dll" || goto :fail

echo jxl-thumb installed in %DEST%
echo New thumbnails use it now. Thumbnails Windows already cached stay as they
echo are until the cache is cleared - see clear_thumbnail_cache.cmd.
exit /b 0

:no_dll
echo jxl_thumb.dll not found next to install.cmd or in build\.
exit /b 1
:fail
echo INSTALL FAILED
exit /b 1
