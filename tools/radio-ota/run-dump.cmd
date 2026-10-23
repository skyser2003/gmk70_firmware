@echo off
setlocal
cd /d "%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0dump-radio.ps1"
set "gmk70_dump_result=%ERRORLEVEL%"
echo.
pause
exit /b %gmk70_dump_result%
