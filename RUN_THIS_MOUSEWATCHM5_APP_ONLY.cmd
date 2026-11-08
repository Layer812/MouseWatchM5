REM Copyright (c) 2026 Layer812. SPDX-License-Identifier: GPL-3.0-or-later
@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
set "PYP=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
set "ESPTOOL=%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py"
set "PORT=%~1"
if not exist "%PIO%" (
  where pio >nul 2>&1
  if errorlevel 1 (
    echo ERROR: PlatformIO not found. Install PlatformIO or open in VS Code.
    exit /b 1
  )
  set "PIO=pio"
)
if not exist "%PYP%" (
  echo ERROR: PlatformIO Python not found: "%PYP%"
  exit /b 1
)
if not exist "%ESPTOOL%" (
  echo ERROR: PlatformIO esptool not found: "%ESPTOOL%"
  exit /b 1
)
echo [1/2] Building MouseWatchM5 R2 FIX13 (BLE overlay; Cat Clock dual core) ...
"%PIO%" run -e stopwatch_mouse
if errorlevel 1 (
  echo ABORT: Build failed; NOTHING flashed.
  exit /b 1
)
set "FIRMWARE=%~dp0.pio\build\stopwatch_mouse\firmware.bin"
if not exist "%FIRMWARE%" (
  echo ABORT: firmware.bin not created; NOTHING flashed.
  exit /b 1
)
echo [2/2] Reading live target and flashing APP-ONLY ...
if "%PORT%"=="" (
  "%PYP%" "%~dp0flash_app_only.py" --python "%PYP%" --esptool "%ESPTOOL%" --firmware "%FIRMWARE%"
) else (
  "%PYP%" "%~dp0flash_app_only.py" --python "%PYP%" --esptool "%ESPTOOL%" --firmware "%FIRMWARE%" --port "%PORT%"
)
if errorlevel 1 (
  echo ABORT: StopWatch not flashed or flash failed. See message above.
  exit /b 1
)
echo PASS: MouseWatchM5 R2 FIX13 APP-ONLY written and device reset.
exit /b 0
