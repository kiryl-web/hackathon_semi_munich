@echo off
title PQC demo server (simple UI) - close this window to stop
cd /d "%~dp0"
py -3.13 server.py --kiosk-simple
pause
