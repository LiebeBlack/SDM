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
- Anti-HTML guard: an HTML response (login/error page) is never saved as the
  file; SDM automatically requests a fresh link from the browser instead
- Browser-style context menu capture: download a link, a video or every
  downloadable link on the page from the extension
- Exact-byte resume with ETag validation and dynamic re-segmentation

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
The Chrome/Edge/Brave/Vivaldi/Opera/Firefox extension is in `extension/`:
- `background/service_worker.js` - Main service worker with Native Messaging and WebSocket
- `content/media_detector.js` - Detects HLS/DASH and video elements
- `content/content_script.js` - Floating "download this video" button anchored to the player
- `popup/popup.html` - Popup interface
- `popup/popup.css` - Styles
- `popup/popup.js` - Popup logic

### Zero-configuration setup (no manual IDs, no tokens)
1. Install SDM (installer or portable zip) and run it once.
2. Load the `extension/` folder as an unpacked extension (Chromium) or the
   generated `dist/sdm-extension-firefox-<version>.zip` (Firefox).
3. Done. SDM discovers the extension ID by reading the browser profiles
   (see `src/bridge/NativeBridgeRegistrar.cpp`), writes the Native Messaging
   manifests for every installed browser and retries every minute until the
   extension appears. The extension then receives the local WebSocket
   `port:token` through the native channel, so the fallback channel is
   configured on its own as well.

Firefox does not need discovery: the packaged build declares the fixed id
`sdm@sdm.local` (`browser_specific_settings.gecko.id`).

### Build the store packages
```bash
python scripts/package_extension.py
# dist/sdm-extension-<ver>.zip         -> Chromium (Chrome/Edge/Brave/Vivaldi/Opera)
# dist/sdm-extension-firefox-<ver>.zip -> Firefox MV3 (event page + fixed id)
```

### Development
1. Load the extension in `chrome://extensions/` with "Developer mode" on
2. Nothing else: the native host is registered automatically by SDM
3. Use `chrome.runtime.sendMessage` to interact

## Protocol
See `PROTOCOL.md` for the complete JSON message protocol between extension and native host.

## License
See LICENSE for licensing information.