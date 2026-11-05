@echo off
rem Undoes enable_explorer_details.cmd (only what it changed).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0explorer_details.ps1" disable
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
