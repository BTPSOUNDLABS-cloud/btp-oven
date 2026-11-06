@echo off
title BtP Oven Installer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-BtPOven.ps1" %*
echo.
pause
