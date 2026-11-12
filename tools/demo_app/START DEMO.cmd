@echo off
title PQC demo server - close this window to stop
cd /d "%~dp0"
py -3.13 server.py --kiosk
pause
