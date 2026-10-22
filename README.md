# S-Download Manager (SDM)

## Overview
S-Download Manager is a multithreaded download accelerator with HLS/DASH stream support and native messaging bridges for browser integration.

## Features
- Multithreaded download management with libcurl
- HLS (.m3u8) and DASH (.mpd) stream support
- Native bridging via Chrome Native Messaging for MV3 extensions
- WebSocket fallback protocol for browser communication
- Download pausing, resuming, and error recovery
- Configurable thread counts and connection settings
- Blocked sites list and extension-based capture

## Build & Install

### Linux
```bash
cmake --preset release
cmake --build --preset release
sudo cmake --install --prefix /usr
```

### Windows
Use the Visual Studio preset or build with MSVC toolchain.

### NSIS Installer
Build the installer via the GitHub Actions workflow or locally:
```powershell
makensis packaging/windows/installer.nsi
```

## Extension (Manifest V3)
The Chrome/Edge/Brave extension is in `extension/`:
- `background/service_worker.js` - Main service worker with Native Messaging and WebSocket
- `content/media_detector.js` - Detects HLS/DASH and video elements
- `content/content_script.js` - Floating button UI
- `popup/popup.html` - Popup interface
- `popup/popup.css` - Styles
- `popup/popup.js` - Popup logic

### Development
1. Load the extension in `chrome://extensions/` with "Developer mode" on
2. Set the native host path or use WebSocket fallback
3. Use `chrome.runtime.sendMessage` to interact

## Protocol
See `PROTOCOL.md` for the complete JSON message protocol between extension and native host.

## License
See LICENSE for licensing information.