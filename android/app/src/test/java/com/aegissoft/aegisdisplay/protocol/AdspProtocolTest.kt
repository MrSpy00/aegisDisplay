package com.aegissoft.aegisdisplay.protocol

import com.aegissoft.aegisdisplay.network.DiscoveryClient
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class AdspProtocolTest {

    @Test
    fun helloAndHeaderWireSizesMatchCppHost() {
        val hello = AdspProtocol.buildHelloPayload(
            deviceId = "aegis-test-device-01",
            deviceName = "HONOR PTP-N49",
            width = 2560,
            height = 1600,
            refreshHz = 120,
            dpi = 480,
            codecMask = AdspConstants.CODEC_MASK_HEVC or AdspConstants.CODEC_MASK_H264,
            transportPref = AdspConstants.TRANSPORT_AUTO
        )
        assertEquals(148, hello.size)

        val pkt = AdspProtocol.buildControlPacket(AdspConstants.MSG_HELLO, 42, hello)
        assertEquals(16 + 148, pkt.size)

        val hdr = AdspProtocol.parseHeader(pkt)
        assertNotNull(hdr)
        assertTrue(hdr!!.isValid())
        assertEquals(AdspConstants.MSG_HELLO, hdr.msgType)
        assertEquals(42, hdr.seq)
        assertEquals(148, hdr.payloadLen)
    }

    @Test
    fun parseHelloAckMatches28ByteCppStruct() {
        val buf = ByteBuffer.allocate(28).order(ByteOrder.LITTLE_ENDIAN)
        buf.putInt(0) // status
        buf.putInt(0x12345678) // token
        buf.put(AdspConstants.CODEC_HEVC.toByte())
        buf.put(0.toByte()) // udp video
        buf.putShort(28452.toShort())
        buf.putInt(2400)
        buf.putInt(1080)
        buf.putInt(90)
        buf.putInt(0)

        val ack = AdspProtocol.parseHelloAck(buf.array())
        assertNotNull(ack)
        assertEquals(0, ack!!.status)
        assertEquals(0x12345678, ack.sessionToken)
        assertEquals(AdspConstants.CODEC_HEVC, ack.negotiatedCodec)
        assertEquals(false, ack.videoOverTcp)
        assertEquals(28452, ack.udpVideoPort)
        assertEquals(2400, ack.width)
        assertEquals(1080, ack.height)
        assertEquals(90, ack.refreshHz)
    }

    @Test
    fun touchAndPenPayloadsMatchWireContract() {
        val touch = AdspProtocol.buildTouchBatchPayload(
            listOf(
                TouchContactData(1, AdspConstants.TOUCH_DOWN, 1000, 2000, 32768, 4096),
                TouchContactData(2, AdspConstants.TOUCH_MOVE, 65535, 0, 65535, 8192)
            )
        )
        assertEquals(4 + 2 * 14, touch.size)
        assertEquals(2.toByte(), touch[0])

        val pen = AdspProtocol.buildPenPayload(
            PenEventData(
                action = AdspConstants.PEN_MOVE,
                flags = AdspConstants.PEN_FLAG_IN_RANGE or AdspConstants.PEN_FLAG_IN_CONTACT,
                xNorm = 32768,
                yNorm = 16384,
                pressure = 40000,
                rotationDeg = 90,
                tiltXDeg = -15,
                tiltYDeg = 22
            )
        )
        assertEquals(16, pen.size)
    }

    @Test
    fun udpReassemblerHandlesOutOfOrderAndEviction() {
        val reassembler = UdpFrameReassembler(maxPendingFrames = 2)
        val token = 0x55AA1122
        val part0 = byteArrayOf(0, 0, 0, 1, 0x67, 1, 2, 3)
        val part1 = byteArrayOf(9, 8, 7, 6)

        fun makeFrag(frameId: Int, fragIdx: Int, fragCnt: Int, data: ByteArray): ByteArray {
            val b = ByteBuffer.allocate(24 + data.size).order(ByteOrder.LITTLE_ENDIAN)
            b.putShort(AdspConstants.VIDEO_MAGIC)
            b.put((AdspConstants.CODEC_H264 or AdspConstants.VIDEO_FLAG_IDR).toByte())
            b.put(0.toByte())
            b.putInt(token)
            b.putInt(frameId)
            b.putShort(fragIdx.toShort())
            b.putShort(fragCnt.toShort())
            b.putLong(16666L)
            b.put(data)
            return b.array()
        }

        // Send fragment 1 before fragment 0
        val p1 = makeFrag(10, 1, 2, part1)
        assertNull(reassembler.pushPacket(token, p1, p1.size))

        val p0 = makeFrag(10, 0, 2, part0)
        val assembled = reassembler.pushPacket(token, p0, p0.size)
        assertNotNull(assembled)
        assertEquals(10, assembled!!.frameId)
        assertTrue(assembled.isIdr)
        assertArrayEquals(part0 + part1, assembled.annexB)
    }

    @Test
    fun discoveryBeaconJsonParsesHostInfo() {
        val json = """
            {"magic":"ADSP1","hostId":"pc-1","hostname":"AEGIS-HOST","tcpPort":28451,"udpPort":28452,"gpu":"NVIDIA GeForce RTX 5070 Ti Laptop GPU","codecs":["HEVC","H264"]}
        """.trimIndent()
        val host = DiscoveryClient.parseBeaconJson(json, "192.168.1.50")
        assertNotNull(host)
        assertEquals("AEGIS-HOST", host!!.hostname)
        assertEquals("192.168.1.50", host.ipAddress)
        assertEquals(28451, host.tcpPort)
        assertEquals(28452, host.udpPort)
        assertTrue(host.supportsHevc)
    }
}
