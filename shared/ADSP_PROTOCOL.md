# ADSP v1 — aegisDisplay Stream Protocol Specification

> **Protocol ID:** `ADSP v1` (`magic = 'A','D','S','P'` = `0x50534441` LE)  
> **Byte Order:** Little-Endian across all headers and payloads  
> **Ports:** UDP `28450` (Discovery), TCP `28451` (Control + Input + Optional Framed Video), UDP `28452` (Low-Latency Video)

---

## 1. Control Channel Framing (`TCP 28451`)

Every control packet starts with a fixed **16-byte** `AdspHeader`:

| Offset | Size | Field | Description |
| :--- | :--- | :--- | :--- |
| `0` | `4` | `magic` | `0x50534441` (`"ADSP"` in ASCII bytes `'A','D','S','P'`) |
| `4` | `1` | `version` | `1` |
| `5` | `1` | `msgType` | Message enum (`0x01..0xFF`) |
| `6` | `2` | `flags` | Bitflags (e.g. compressed/urgent) |
| `8` | `4` | `seq` | Monotonic sequence number |
| `12` | `4` | `payloadLen` | Payload length in bytes (`0..16 MiB`) |

### Control `msgType` Values
* `0x01` `HELLO` (C→H, 148 B): `deviceId[64]`, `deviceName[64]`, `width(u32)`, `height(u32)`, `refreshHz(u32)`, `dpi(u32)`, `codecMask(u8)` (`1=H264, 2=HEVC`), `transportPref(u8)` (`0=AUTO, 1=UDP_VIDEO, 2=TCP_VIDEO`), `reserved(u16)`
* `0x02` `HELLO_ACK` (H→C, 28 B): `status(u32)` (`0=OK`), `sessionToken(u32)`, `negotiatedCodec(u8)` (`1=H264, 2=HEVC`), `videoOverTcp(u8)`, `udpVideoPort(u16)`, `width(u32)`, `height(u32)`, `refreshHz(u32)`, `reserved(u32)`
* `0x03` `DISPLAY_CONFIG_REQ` (C→H, 24 B): `width(u32)`, `height(u32)`, `refreshHz(u32)`, `bitrateKbps(u32)`, `codec(u8)`, `scaleMode(u8)`, `lowLatency(u8)`, `reserved(u8)`, `reserved2(u32)`
* `0x04` `DISPLAY_CONFIG_ACK` (H→C, 16 B): `status(u32)`, `actualWidth(u32)`, `actualHeight(u32)`, `actualRefreshHz(u32)`
* `0x05` `PING` (Bidirectional, 8 B): `timestampUs(u64)`
* `0x06` `PONG` (Bidirectional, 8 B): `echoTimestampUs(u64)`
* `0x07` `FRAME_ACK` (C→H, 20 B): `frameId(u32)`, `recvTimeUs(u64)`, `decodeUs(u32)`, `lossPermille(u16)`, `jitterMs(u16)`
* `0x08` `KEYFRAME_REQ` (C→H, 8 B): `lastGoodFrameId(u32)`, `reason(u32)`
* `0x10` `INPUT_TOUCH_BATCH` (C→H, `4 + count*14` B): `count(u8)`, `reserved[3]`; per contact (`14 B`): `pointerId(u32)`, `action(u8)` (`0=DOWN,1=MOVE,2=UP,3=CANCEL`), `reserved(u8)`, `xNorm(u16)`, `yNorm(u16)`, `pressure(u16)` (`0..1024`), `contactSize(u16)`
* `0x11` `INPUT_PEN` (C→H, 16 B): `action(u8)` (`0=DOWN,1=MOVE,2=UP,3=HOVER`), `flags(u8)` (`bit0=IN_RANGE, bit1=IN_CONTACT, bit2=BARREL, bit3=ERASER`), `xNorm(u16)`, `yNorm(u16)`, `pressure(u16)` (`0..1024`), `rotation(u16)` (`0..359`), `tiltX(i8)` (`-90..90`), `tiltY(i8)` (`-90..90`), `reserved(u32)`
* `0x12` `INPUT_MOUSE` (C→H, 12 B): `flags(u16)`, `reserved(u16)`, `xNorm(u16)`, `yNorm(u16)`, `wheelDelta(i16)`, `hWheelDelta(i16)`
* `0x13` `INPUT_KEY` (C→H, 8 B): `scanCode(u16)`, `vkCode(u16)`, `isDown(u8)`, `isExtended(u8)`, `reserved(u16)`
* `0x20` `VIDEO_FRAME_TCP` (H→C, `24 + payloadLen` B): Same `AvPacketHeader` + full Annex-B frame payload (used on ADB tunnel / TCP fallback)
* `0x7F` `DISCONNECT` (Bidirectional, 4 B): `reasonCode(u32)`

---

## 2. Video Channel Framing (`UDP 28452` & TCP `0x20`)

Every video datagram starts with a fixed **24-byte** `AvPacketHeader`, followed by up to `1360` bytes of Annex-B slice/NAL payload:

| Offset | Size | Field | Description |
| :--- | :--- | :--- | :--- |
| `0` | `2` | `magic` | `0x5641` (`'A','V'`) |
| `2` | `1` | `codecFlags` | `bits[0..1]`: codec (`1=H264, 2=HEVC`); `bit2`: `IS_IDR`; `bit3`: `CONTAINS_CSD` |
| `3` | `1` | `reserved` | `0` |
| `4` | `4` | `sessionToken` | Assigned in `HELLO_ACK` |
| `8` | `4` | `frameId` | Monotonic frame index |
| `12` | `2` | `fragIdx` | Zero-based fragment index (`0..fragCnt-1`) |
| `14` | `2` | `fragCnt` | Total fragments for `frameId` |
| `16` | `8` | `ptsUs` | Capture timestamp in microseconds (`QueryPerformanceCounter`) |

*(Client sends an initial empty/hello 8-byte registration datagram `'A','V','H','I' + sessionToken(u32)` to `UDP 28452` immediately after `HELLO_ACK` so NAT/Windows Firewall maps the client's UDP source port deterministically.)*
