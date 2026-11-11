@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -NoExit -File "%~dp0demo_3_spdm.ps1" -Op measure -Image resp_v1_0_0_boot.hex
