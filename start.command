#!/bin/bash
# Bunker Survivors (macOS) - double-click in Finder, or run ./start.command in Terminal.
# Starts the companion server (streams StarCraft II art from your own install) and opens the game.
# Phones on the same Wi-Fi can open the LAN address printed below.
cd "$(dirname "$0")" || exit 1
if ! command -v node >/dev/null 2>&1; then
  echo "Node.js is required: https://nodejs.org (or: brew install node)"; read -r -p "Press Return to close."; exit 1
fi
if [ ! -d node_modules ]; then echo "Installing dependencies..."; npm install --no-audit --no-fund || exit 1; fi
(sleep 3; open "http://localhost:8642/") &
node server/index.js
read -r -p "Server stopped. Press Return to close."
