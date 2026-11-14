@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch_atm10_windows.ps1" %*
set "ATM10_EXIT=%ERRORLEVEL%"
if not "%ATM10_EXIT%"=="0" if not defined CI pause
exit /b %ATM10_EXIT%
