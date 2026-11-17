@echo off
setlocal enabledelayedexpansion

title ADF4351 ESP32 firmware flasher

echo ==========================================
echo   ADF4351 ESP32 DevKit V1 firmware flasher
echo ==========================================
echo.

if not exist "bootloader.bin" (
  echo [ERROR] bootloader.bin not found next to this script.
  echo Download and extract the esp32-firmware artifact from GitHub Actions.
  pause
  exit /b 1
)

if not exist "partitions.bin" (
  echo [ERROR] partitions.bin not found next to this script.
  pause
  exit /b 1
)

if not exist "boot_app0.bin" (
  echo [ERROR] boot_app0.bin not found next to this script.
  pause
  exit /b 1
)

if not exist "firmware.bin" (
  echo [ERROR] firmware.bin not found next to this script.
  pause
  exit /b 1
)

echo Found required files:
echo   bootloader.bin
echo   partitions.bin
echo   boot_app0.bin
echo   firmware.bin
echo.

where py >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python is not installed or 'py' is unavailable.
  echo Install Python from https://www.python.org/downloads/
  pause
  exit /b 1
)

py -m esptool version >nul 2>nul
if errorlevel 1 (
  echo esptool is not installed. Installing...
  py -m pip install --upgrade esptool
  if errorlevel 1 (
    echo [ERROR] Failed to install esptool.
    pause
    exit /b 1
  )
)

echo Connect ESP32 DevKit V1 by USB cable.
echo If the board is not detected: hold BOOT, briefly press EN/RESET,
echo then release BOOT.
echo.
set /p PORT=Enter COM port, for example COM5:

if "%PORT%"=="" (
  echo [ERROR] COM port is required.
  pause
  exit /b 1
)

echo.
echo Flashing ESP32 on %PORT%...
echo.

py -m esptool --chip esp32 --port %PORT% --baud 460800 --before default-reset --after hard-reset write-flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin

if errorlevel 1 (
  echo.
  echo [ERROR] Flashing failed.
  echo Try holding BOOT, press EN/RESET, release BOOT, then run this script again.
) else (
  echo.
  echo Firmware flashed successfully.
  echo Connect to Wi-Fi ADF4351-ESP32, password adf4351rf.
  echo Open http://192.168.4.2
)

echo.
pause
