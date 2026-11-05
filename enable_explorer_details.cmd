@echo off
rem Shows the photo details of .jxl files in Explorer (camera, lens, ISO, date
rem taken, title): registers Windows' own image property handler for .jxl.
rem Asks for administrator rights once. See explorer_details.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0explorer_details.ps1" enable
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
