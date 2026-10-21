@echo off
rem Bunker Survivors - starts the companion server (streams StarCraft II art from your own install)
rem and opens the game. Phones on the same Wi-Fi can open the LAN address printed below.
cd /d "%~dp0"
where node >nul 2>nul || (echo Node.js is required: https://nodejs.org & pause & exit /b 1)
if not exist node_modules (echo Installing dependencies... & call npm install --no-audit --no-fund & node scripts\patch-viewer.js)
start "" http://localhost:8642/
node server\index.js
pause
