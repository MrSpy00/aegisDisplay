package com.aegissoft.aegisdisplay.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets

object AdspConstants {
    const val CONTROL_MAGIC: Int = 0x50534441 // "ADSP" LE
    const val VIDEO_MAGIC: Short = 0x5641.toShort() // "AV" LE
    const val UDP_REG_MAGIC: Int = 0x49485641 // "AVHI" LE
    const val PROTOCOL_VERSION: Byte = 1

    const val DEFAULT_DISCOVERY_PORT: Int = 28450
    const val DEFAULT_CONTROL_PORT: Int = 28451
    const val DEFAULT_VIDEO_PORT: Int = 28452

    const val HEADER_SIZE: Int = 16
    const val HELLO_SIZE: Int = 148
    const val HELLO_ACK_SIZE: Int = 28
    const val DISPLAY_CONFIG_REQ_SIZE: Int = 24
    const val DISPLAY_CONFIG_ACK_SIZE: Int = 16
    const val TOUCH_CONTACT_SIZE: Int = 14
    const val INPUT_PEN_SIZE: Int = 16
    const val FRAME_ACK_SIZE: Int = 20
    const val KEYFRAME_REQ_SIZE: Int = 8
    const val AV_HEADER_SIZE: Int = 24
    const val UDP_REG_SIZE: Int = 8

    const val MAX_CONTROL_PAYLOAD: Int = 16 * 1024 * 1024

    const val CODEC_NONE: Int = 0
    const val CODEC_H264: Int = 1
    const val CODEC_HEVC: Int = 2

    const val CODEC_MASK_H264: Int = 0x01
    const val CODEC_MASK_HEVC: Int = 0x02

    const val TRANSPORT_AUTO: Int = 0
    const val TRANSPORT_UDP: Int = 1
    const val TRANSPORT_TCP: Int = 2

    const val VIDEO_FLAG_IDR: Int = 0x04
    const val VIDEO_FLAG_CSD: Int = 0x08

    const val MSG_HELLO: Byte = 0x01
    const val MSG_HELLO_ACK: Byte = 0x02
    const val MSG_DISPLAY_CONFIG_REQ: Byte = 0x03
    const val MSG_DISPLAY_CONFIG_ACK: Byte = 0x04
    const val MSG_PING: Byte = 0x05
    const val MSG_PONG: Byte = 0x06
    const val MSG_FRAME_ACK: Byte = 0x07
    const val MSG_KEYFRAME_REQ: Byte = 0x08
    const val MSG_INPUT_TOUCH_BATCH: Byte = 0x10
    const val MSG_INPUT_PEN: Byte = 0x11
    const val MSG_INPUT_MOUSE: Byte = 0x12
    const val MSG_INPUT_KEY: Byte = 0x13
    const val MSG_VIDEO_FRAME_TCP: Byte = 0x20
    const val MSG_DISCONNECT: Byte = 0x7F

    const val TOUCH_DOWN: Byte = 0
    const val TOUCH_MOVE: Byte = 1
    const val TOUCH_UP: Byte = 2
    const val TOUCH_CANCEL: Byte = 3

    const val PEN_DOWN: Byte = 0
    const val PEN_MOVE: Byte = 1
    const val PEN_UP: Byte = 2
    const val PEN_HOVER: Byte = 3

    const val PEN_FLAG_IN_RANGE: Int = 0x01
    const val PEN_FLAG_IN_CONTACT: Int = 0x02
    const val PEN_FLAG_BARREL: Int = 0x04
    const val PEN_FLAG_ERASER: Int = 0x08

    const val MOUSE_MOVE: Int = 0x0001
    const val MOUSE_LEFT_DOWN: Int = 0x0002
    const val MOUSE_LEFT_UP: Int = 0x0004
    const val MOUSE_RIGHT_DOWN: Int = 0x0008
    const val MOUSE_RIGHT_UP: Int = 0x0010
    const val MOUSE_MIDDLE_DOWN: Int = 0x0020
    const val MOUSE_MIDDLE_UP: Int = 0x0040
    const val MOUSE_WHEEL: Int = 0x0080
    const val MOUSE_HWHEEL: Int = 0x0100
    const val MOUSE_RELATIVE: Int = 0x0200
}

data class AdspHeader(
    val magic: Int,
    val version: Byte,
    val msgType: Byte,
    val flags: Short,
    val seq: Int,
    val payloadLen: Int
) {
    fun isValid(): Boolean =
        magic == AdspConstants.CONTROL_MAGIC &&
            version == AdspConstants.PROTOCOL_VERSION &&
            payloadLen >= 0 &&
            payloadLen <= AdspConstants.MAX_CONTROL_PAYLOAD
}

data class HelloAck(
    val status: Int,
    val sessionToken: Int,
    val negotiatedCodec: Int,
    val videoOverTcp: Boolean,
    val udpVideoPort: Int,
    val width: Int,
    val height: Int,
    val refreshHz: Int
)

data class TouchContactData(
    val pointerId: Int,
    val action: Byte,
    val xNorm: Int,
    val yNorm: Int,
    val pressure: Int,
    val contactSize: Int
)

data class PenEventData(
    val action: Byte,
    val flags: Int,
    val xNorm: Int,
    val yNorm: Int,
    val pressure: Int,
    val rotationDeg: Int,
    val tiltXDeg: Byte,
    val tiltYDeg: Byte
)

data class MouseEventData(
    val flags: Int,
    val xValue: Int,
    val yValue: Int,
    val wheelDelta: Short = 0,
    val hWheelDelta: Short = 0
)

data class DecodedAvPacketHeader(
    val codec: Int,
    val isIdr: Boolean,
    val containsCsd: Boolean,
    val sessionToken: Int,
    val frameId: Int,
    val fragIdx: Int,
    val fragCnt: Int,
    val ptsUs: Long
)

data class AssembledVideoFrame(
    val frameId: Int,
    val ptsUs: Long,
    val codec: Int,
    val isIdr: Boolean,
    val containsCsd: Boolean,
    val annexB: ByteArray
)

object AdspProtocol {

    private fun leBuffer(capacity: Int): ByteBuffer =
        ByteBuffer.allocate(capacity).order(ByteOrder.LITTLE_ENDIAN)

    private fun putFixedCString(buf: ByteBuffer, value: String, capacity: Int) {
        val raw = value.toByteArray(StandardCharsets.UTF_8)
        val out = ByteArray(capacity)
        val count = minOf(raw.size, capacity - 1)
        System.arraycopy(raw, 0, out, 0, count)
        buf.put(out)
    }

    fun buildControlPacket(
        msgType: Byte,
        seq: Int,
        payload: ByteArray = ByteArray(0),
        flags: Short = 0
    ): ByteArray {
        val buf = leBuffer(AdspConstants.HEADER_SIZE + payload.size)
        buf.putInt(AdspConstants.CONTROL_MAGIC)
        buf.put(AdspConstants.PROTOCOL_VERSION)
        buf.put(msgType)
        buf.putShort(flags)
        buf.putInt(seq)
        buf.putInt(payload.size)
        if (payload.isNotEmpty()) {
            buf.put(payload)
        }
        return buf.array()
    }

    fun parseHeader(bytes: ByteArray, offset: Int = 0): AdspHeader? {
        if (bytes.size - offset < AdspConstants.HEADER_SIZE) return null
        val buf = ByteBuffer.wrap(bytes, offset, AdspConstants.HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        return AdspHeader(
            magic = buf.int,
            version = buf.get(),
            msgType = buf.get(),
            flags = buf.short,
            seq = buf.int,
            payloadLen = buf.int
        )
    }

    fun buildHelloPayload(
        deviceId: String,
        deviceName: String,
        width: Int,
        height: Int,
        refreshHz: Int,
        dpi: Int,
        codecMask: Int,
        transportPref: Int
    ): ByteArray {
        val buf = leBuffer(AdspConstants.HELLO_SIZE)
        putFixedCString(buf, deviceId, 64)
        putFixedCString(buf, deviceName, 64)
        buf.putInt(width)
        buf.putInt(height)
        buf.putInt(refreshHz)
        buf.putInt(dpi)
        buf.put((codecMask and 0xFF).toByte())
        buf.put((transportPref and 0xFF).toByte())
        buf.putShort(0)
        return buf.array()
    }

    fun parseHelloAck(payload: ByteArray): HelloAck? {
        if (payload.size < AdspConstants.HELLO_ACK_SIZE) return null
        val buf = ByteBuffer.wrap(payload, 0, AdspConstants.HELLO_ACK_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        val status = buf.int
        val token = buf.int
        val codec = buf.get().toInt() and 0xFF
        val overTcp = (buf.get().toInt() and 0xFF) != 0
        val udpPort = buf.short.toInt() and 0xFFFF
        val w = buf.int
        val h = buf.int
        val hz = buf.int
        buf.int // reserved
        return HelloAck(
            status = status,
            sessionToken = token,
            negotiatedCodec = codec,
            videoOverTcp = overTcp,
            udpVideoPort = udpPort,
            width = w,
            height = h,
            refreshHz = hz
        )
    }

    fun buildUdpRegisterPacket(sessionToken: Int): ByteArray {
        val buf = leBuffer(AdspConstants.UDP_REG_SIZE)
        buf.putInt(AdspConstants.UDP_REG_MAGIC)
        buf.putInt(sessionToken)
        return buf.array()
    }

    fun buildDisplayConfigReq(
        width: Int,
        height: Int,
        refreshHz: Int,
        bitrateKbps: Int,
        codec: Int,
        scaleMode: Int,
        lowLatency: Boolean
    ): ByteArray {
        val buf = leBuffer(AdspConstants.DISPLAY_CONFIG_REQ_SIZE)
        buf.putInt(width)
        buf.putInt(height)
        buf.putInt(refreshHz)
        buf.putInt(bitrateKbps)
        buf.put(codec.toByte())
        buf.put(scaleMode.toByte())
        buf.put(if (lowLatency) 1.toByte() else 0.toByte())
        buf.put(0.toByte())
        buf.putInt(0)
        return buf.array()
    }

    fun buildTouchBatchPayload(contacts: List<TouchContactData>): ByteArray {
        val count = minOf(contacts.size, 10)
        val buf = leBuffer(4 + count * AdspConstants.TOUCH_CONTACT_SIZE)
        buf.put(count.toByte())
        buf.put(0.toByte())
        buf.put(0.toByte())
        buf.put(0.toByte())
        for (i in 0 until count) {
            val c = contacts[i]
            buf.putInt(c.pointerId)
            buf.put(c.action)
            buf.put(0.toByte())
            buf.putShort(c.xNorm.coerceIn(0, 65535).toShort())
            buf.putShort(c.yNorm.coerceIn(0, 65535).toShort())
            buf.putShort(c.pressure.coerceIn(0, 65535).toShort())
            buf.putShort(c.contactSize.coerceIn(0, 65535).toShort())
        }
        return buf.array()
    }

    fun buildPenPayload(pen: PenEventData): ByteArray {
        val buf = leBuffer(AdspConstants.INPUT_PEN_SIZE)
        buf.put(pen.action)
        buf.put((pen.flags and 0xFF).toByte())
        buf.putShort(pen.xNorm.coerceIn(0, 65535).toShort())
        buf.putShort(pen.yNorm.coerceIn(0, 65535).toShort())
        buf.putShort(pen.pressure.coerceIn(0, 65535).toShort())
        buf.putShort(pen.rotationDeg.coerceIn(0, 359).toShort())
        buf.put(pen.tiltXDeg)
        buf.put(pen.tiltYDeg)
        buf.putInt(0)
        return buf.array()
    }

    fun buildMousePayload(mouse: MouseEventData): ByteArray {
        val buf = leBuffer(12)
        buf.putShort(mouse.flags.toShort())
        buf.putShort(0)
        buf.putShort(mouse.xValue.toShort())
        buf.putShort(mouse.yValue.toShort())
        buf.putShort(mouse.wheelDelta)
        buf.putShort(mouse.hWheelDelta)
        return buf.array()
    }

    fun buildFrameAckPayload(
        frameId: Int,
        recvTimeUs: Long,
        decodeUs: Int,
        lossPermille: Int,
        jitterMs: Int
    ): ByteArray {
        val buf = leBuffer(AdspConstants.FRAME_ACK_SIZE)
        buf.putInt(frameId)
        buf.putLong(recvTimeUs)
        buf.putInt(decodeUs)
        buf.putShort(lossPermille.coerceIn(0, 65535).toShort())
        buf.putShort(jitterMs.coerceIn(0, 65535).toShort())
        return buf.array()
    }

    fun buildKeyframeReqPayload(lastGoodFrameId: Int, reason: Int = 1): ByteArray {
        val buf = leBuffer(AdspConstants.KEYFRAME_REQ_SIZE)
        buf.putInt(lastGoodFrameId)
        buf.putInt(reason)
        return buf.array()
    }

    fun buildLongPayload(value: Long): ByteArray {
        val buf = leBuffer(8)
        buf.putLong(value)
        return buf.array()
    }

    fun parseAvPacketHeader(bytes: ByteArray, offset: Int = 0, length: Int = bytes.size - offset): DecodedAvPacketHeader? {
        if (length < AdspConstants.AV_HEADER_SIZE) return null
        val buf = ByteBuffer.wrap(bytes, offset, AdspConstants.AV_HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        val magic = buf.short
        if (magic != AdspConstants.VIDEO_MAGIC) return null
        val codecFlags = buf.get().toInt() and 0xFF
        buf.get() // reserved
        val token = buf.int
        val frameId = buf.int
        val fragIdx = buf.short.toInt() and 0xFFFF
        val fragCnt = buf.short.toInt() and 0xFFFF
        val ptsUs = buf.long
        return DecodedAvPacketHeader(
            codec = codecFlags and 0x03,
            isIdr = (codecFlags and AdspConstants.VIDEO_FLAG_IDR) != 0,
            containsCsd = (codecFlags and AdspConstants.VIDEO_FLAG_CSD) != 0,
            sessionToken = token,
            frameId = frameId,
            fragIdx = fragIdx,
            fragCnt = fragCnt,
            ptsUs = ptsUs
        )
    }
}

class UdpFrameReassembler(private val maxPendingFrames: Int = 8) {
    private class PendingFrame(
        val frameId: Int,
        val ptsUs: Long,
        val codec: Int,
        val isIdr: Boolean,
        val containsCsd: Boolean,
        val fragCnt: Int
    ) {
        val chunks = arrayOfNulls<ByteArray>(fragCnt)
        var receivedCount = 0
        var totalBytes = 0
    }

    private val pending = LinkedHashMap<Int, PendingFrame>()
    var droppedFrames: Long = 0
        private set

    fun pushPacket(
        expectedSessionToken: Int,
        packet: ByteArray,
        packetLength: Int
    ): AssembledVideoFrame? {
        val header = AdspProtocol.parseAvPacketHeader(packet, 0, packetLength) ?: return null
        if (expectedSessionToken != 0 && header.sessionToken != expectedSessionToken) return null
        if (header.fragCnt <= 0 || header.fragIdx >= header.fragCnt) return null

        val payloadLen = packetLength - AdspConstants.AV_HEADER_SIZE
        if (payloadLen <= 0) return null

        if (header.fragCnt == 1) {
            val data = ByteArray(payloadLen)
            System.arraycopy(packet, AdspConstants.AV_HEADER_SIZE, data, 0, payloadLen)
            return AssembledVideoFrame(
                frameId = header.frameId,
                ptsUs = header.ptsUs,
                codec = header.codec,
                isIdr = header.isIdr,
                containsCsd = header.containsCsd,
                annexB = data
            )
        }

        val entry = pending.getOrPut(header.frameId) {
            while (pending.size >= maxPendingFrames) {
                val oldestKey = pending.keys.first()
                pending.remove(oldestKey)
                droppedFrames++
            }
            PendingFrame(
                frameId = header.frameId,
                ptsUs = header.ptsUs,
                codec = header.codec,
                isIdr = header.isIdr,
                containsCsd = header.containsCsd,
                fragCnt = header.fragCnt
            )
        }

        if (entry.fragCnt != header.fragCnt) return null
        if (entry.chunks[header.fragIdx] == null) {
            val chunk = ByteArray(payloadLen)
            System.arraycopy(packet, AdspConstants.AV_HEADER_SIZE, chunk, 0, payloadLen)
            entry.chunks[header.fragIdx] = chunk
            entry.receivedCount++
            entry.totalBytes += payloadLen
        }

        if (entry.receivedCount == entry.fragCnt) {
            pending.remove(header.frameId)
            val assembled = ByteArray(entry.totalBytes)
            var cursor = 0
            for (c in entry.chunks) {
                val part = c ?: return null
                System.arraycopy(part, 0, assembled, cursor, part.size)
                cursor += part.size
            }
            return AssembledVideoFrame(
                frameId = entry.frameId,
                ptsUs = entry.ptsUs,
                codec = entry.codec,
                isIdr = entry.isIdr,
                containsCsd = entry.containsCsd,
                annexB = assembled
            )
        }
        return null
    }

    fun reset() {
        pending.clear()
        droppedFrames = 0
    }
}
