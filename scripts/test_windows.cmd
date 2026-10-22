@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_windows.ps1" %*
pause
