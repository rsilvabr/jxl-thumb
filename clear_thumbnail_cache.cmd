@echo off
rem Clears Windows' thumbnail cache so every thumbnail is made again - for
rem example after installing jxl-thumb, to replace the ones Windows made before.
rem Explorer closes for a moment (the cache files are open while it runs).
rem All thumbnails are rebuilt on the next browse, not only .jxl ones.
setlocal
echo This restarts Explorer and deletes thumbcache_*.db in
echo %LOCALAPPDATA%\Microsoft\Windows\Explorer
choice /m "Continue"
if errorlevel 2 exit /b 1

taskkill /f /im explorer.exe >nul
timeout /t 2 /nobreak >nul
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
set "LEFT="
if exist "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" set "LEFT=1"
start explorer.exe
if defined LEFT (
    echo Some cache files were in use and stayed. Sign out and back in, then run
    echo this again, or use Disk Cleanup and tick "Thumbnails".
) else (
    echo Thumbnail cache cleared.
)
exit /b 0
