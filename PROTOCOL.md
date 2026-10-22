# S-Download Manager Protocol (PROTOCOL.md)

## Overview
This document defines the JSON message protocol exchanged between the Manifest V3 extension, the native host bridge, and the SDM core library.

## Message Format
All messages follow this JSON structure:
```json
{
  "type": "<message_type>",
  "ref": "<reference_id>",
  "payload": { ... }
}
```

The `ref` field is a unique identifier for tracking responses. The `payload` contains type-specific data.

## Message Types

### From Extension to Native Host / Service Worker

| Type | Description |
|------|-------------|
| `hello` | Initial connection message (sent on native port connect or WS open) |
| `addDownload` | Request to add a new download task |
| `startStream` | Request to start streaming HLS/DASH media |
| `getState` | Request current SDM state (from popup) |
| `setConfig` | Update extension configuration |
| `connect` | Establish connection to native host |
| `disconnect` | Terminate connection |
| `clearCompleted` | Clear completed downloads |
| `sdm:startStream` | Popup-initiated stream start |
| `sdm:addDownload` | Popup-initiated download add |

### From Native Host / Service Worker to Extension

| Type | Description |
|------|-------------|
| `helloAck` | Acknowledgement of hello message |
| `pong` | Keepalive response |
| `status` | Current download status |
| `taskProgress` | Individual task progress updates |
| `taskAdded` | New task added |
| `taskRemoved` | Task removed |
| `tasks` | List of all tasks |
| `toast` | transient notification |
| `urlUpdated` | URL refreshed for expired links |
| `streamVariants` | Available stream qualities |
| `error` | Error message |

### Native Host ↔ SDM Core

The native host (com.sdm.bridge) communicates with the SDM core library via STDIO:
- Receives JSON messages on STDIN
- Sends JSON responses on STDOUT
- Protocol matches the messages defined above

## Protocol Details

### hello / helloAck
```json
// hello (from extension)
{"type": "hello", "ref": "h123", "payload": {"version": "1.0.0"}}

// helloAck (from native host)
{"type": "helloAck", "ref": "h123", "payload": {"version": "1.0.0"}}
```

### addDownload
```json
// addDownload (from extension)
{"type": "addDownload", "ref": "a456", "payload": {
  "url": "https://example.com/file.mp4",
  "destDir": "/home/user/Downloads",
  "fileName": "file.mp4",
  "referer": "https://example.com/",
  "cookies": "session=abc123",
  "userAgent": "Mozilla/5.0",
  "threads": 4,
  "autoStart": true
}}
```

### startStream
```json
// startStream (from extension)
{"type": "startStream", "ref": "st789", "payload": {
  "url": "https://example.com/stream.m3u8",
  "destDir": "",
  "referer": "https://example.com/",
  "cookies": "session=abc123",
  "fileName": "stream.ts"
}}
```

### getState
```json
// getState (from popup/UI)
{"type": "sdm:getState"}

// Response
{"connected": true, "connectionState": "native", "enabled": true, "autoStart": true, "threads": 4, "captureMedia": true, "autoRefreshUrls": true, "blockedSites": [], "captureExtensions": [...], "connectionKey": "port:token", "status": {...}, "lastToast": {...}}
```

### Config
```json
// setConfig (from popup)
{"type": "sdm:setConfig", "payload": {"enabled": true, "autoStart": true, "threads": 4}}

// Response
{"ok": true, "config": {...}}
```

### refreshUrl (spec cap. 10)
```json
// refreshUrl (from native host/service worker)
{"type": "refreshUrl", "ref": "r123", "payload": {
  "taskId": 42,
  "url": "https://new-url.com/file.mp4",
  "cookies": "session=def456",
  "referer": "https://old-url.com/"
}}
```

## Native Messaging Host Details

The native host `com.sdm.bridge` uses STDIO stdio-based communication:
- **Chrome**: `packaging/native-host/com.sdm.bridge.chrome.json`
- **Firefox**: `packaging/native-host/com.sdm.bridge.firefox.json`
- Messages are JSON, one per line, on STDIN/STDOUT

## Error Handling
Errors use the `error` type:
```json
{"type": "error", "ref": "err123", "payload": {"message": "Descriptive error message"}}
```