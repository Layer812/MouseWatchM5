REM Copyright (c) 2026 Layer812. SPDX-License-Identifier: GPL-3.0-or-later
@echo off
setlocal
cd /d "%~dp0"
where pio >nul 2>nul
if errorlevel 1 (
  echo ERROR: PlatformIO CLI ^(pio^) not found. Open this folder in VS Code + PlatformIO.
  exit /b 1
)
pio run -e stopwatch_mouse
if errorlevel 1 exit /b 1
echo.
echo PASS: SOURCE BUILT. No device was flashed.
echo Binary: %CD%\.pio\build\stopwatch_mouse\firmware.bin
endlocal
