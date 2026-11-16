@echo off
setlocal EnableExtensions
title ADF4351 ESP32 Firmware Flasher
cd /d "%~dp0"
echo ==========================================
echo   ADF4351 - ESP32 DevKit V1
echo ==========================================
where arduino-cli >nul 2>nul
if errorlevel 1 (
 echo [ERROR] arduino-cli not found in PATH.
 echo Install: https://arduino.github.io/arduino-cli/
 pause
 exit /b 1
)
echo [1/4] Updating package index...
arduino-cli core update-index
if errorlevel 1 goto failed
echo [2/4] Installing ESP32 core...
arduino-cli core install esp32:esp32
if errorlevel 1 goto failed
echo [3/4] Available serial ports:
arduino-cli board list
set /p PORT=Enter ESP32 COM port (example COM5): 
if "%PORT%"=="" goto failed
if not exist build mkdir build
echo [4/4] Compiling...
arduino-cli compile --fqbn esp32:esp32:esp32 --build-path build ADF4351_ESP32
if errorlevel 1 goto failed
echo Uploading to %PORT%...
arduino-cli upload -p %PORT% --fqbn esp32:esp32:esp32 --input-dir build ADF4351_ESP32
if errorlevel 1 (
 echo Upload failed. If stuck at Connecting, hold BOOT as upload begins.
 goto failed
)
echo.
echo [SUCCESS] Firmware uploaded.
echo Wi-Fi: ADF4351-ESP32  Password: adf4351rf
echo Web: http://192.168.4.2
pause
exit /b 0
:failed
echo.
echo [FAILED] See error above.
pause
exit /b 1
