@echo off
start "" "C:\Program Files\Google\Chrome\Application\chrome.exe" --kiosk --app=http://127.0.0.1:8765/ --user-data-dir="%TEMP%\pqcdemo_chrome" --no-first-run
