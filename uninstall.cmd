@echo off
rem Removes jxl-thumb for the current user. Windows goes back to the thumbnail
rem handler it had before (e.g. the Microsoft JPEG XL extension).
setlocal
set "DEST=%LOCALAPPDATA%\jxl-thumb"

if exist "%DEST%\jxl_thumb.dll" regsvr32 /u /s "%DEST%\jxl_thumb.dll"
rd /s /q "%DEST%" >nul 2>&1
if exist "%DEST%" (
    echo jxl-thumb is unregistered. Explorer still has the DLL open, so the
    echo folder %DEST% could not be deleted yet - delete it after a sign-out.
) else (
    echo jxl-thumb removed.
)
exit /b 0
