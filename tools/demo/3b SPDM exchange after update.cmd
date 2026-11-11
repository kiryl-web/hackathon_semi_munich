@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -NoExit -File "%~dp0demo_3_spdm.ps1" -Op exchange -Message after-update-v1.1
