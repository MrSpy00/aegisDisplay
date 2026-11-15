package com.aegissoft.aegisdisplay.network

import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.protocol.AdspProtocol
import com.aegissoft.aegisdisplay.protocol.AssembledVideoFrame
import com.aegissoft.aegisdisplay.protocol.HelloAck
import com.aegissoft.aegisdisplay.protocol.MouseEventData
import com.aegissoft.aegisdisplay.protocol.PenEventData
import com.aegissoft.aegisdisplay.protocol.TouchContactData
import com.aegissoft.aegisdisplay.protocol.UdpFrameReassembler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.coroutines.coroutineContext
import java.io.BufferedInputStream
import java.io.DataInputStream
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicInteger

data class StreamParams(
    val hostIp: String,
    val tcpPort: Int = AdspConstants.DEFAULT_CONTROL_PORT,
    val deviceId: String,
    val deviceName: String,
    val width: Int,
    val height: Int,
    val refreshHz: Int,
    val dpi: Int,
    val codecMask: Int,
    val transportPref: Int
)

interface SessionCallbacks {
    fun onHandshakeAccepted(ack: HelloAck, isTcpTunnel: Boolean)
    fun onVideoFrameReceived(frame: AssembledVideoFrame)
    fun onRttUpdated(rttMs: Double)
    fun onDisconnected(reason: String)
}

class StreamSessionClient(private val callbacks: SessionCallbacks) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val seq = AtomicInteger(1)
    private val tcpLock = Any()

    @Volatile
    private var tcpSocket: Socket? = null

    @Volatile
    private var tcpOut: OutputStream? = null

    @Volatile
    private var udpSocket: DatagramSocket? = null

    @Volatile
    private var sessionToken: Int = 0

    @Volatile
    private var lastGoodFrameId: Int = 0

    @Volatile
    private var udpDroppedWindow: Long = 0L

    @Volatile
    private var udpDeliveredWindow: Long = 0L

    private var lastLossIdrReqMs: Long = 0L

    private var controlJob: Job? = null
    private var udpJob: Job? = null
    private var pingJob: Job? = null

    fun connect(params: StreamParams) {
        disconnect(sendBye = false)
        controlJob = scope.launch {
            runSession(params)
        }
    }

    private suspend fun runSession(params: StreamParams) {
        var disconnectReason = "Disconnected"
        try {
            val socket = Socket()
            socket.tcpNoDelay = true
            socket.soTimeout = 0
            socket.connect(InetSocketAddress(params.hostIp, params.tcpPort), 4000)
            tcpSocket = socket
            tcpOut = socket.getOutputStream()

            val effectiveTransportPref =
                if (params.hostIp == "127.0.0.1" || params.hostIp == "localhost") {
                    AdspConstants.TRANSPORT_TCP
                } else {
                    params.transportPref
                }

            val helloPayload = AdspProtocol.buildHelloPayload(
                deviceId = params.deviceId,
                deviceName = params.deviceName,
                width = params.width,
                height = params.height,
                refreshHz = params.refreshHz,
                dpi = params.dpi,
                codecMask = params.codecMask,
                transportPref = effectiveTransportPref
            )
            sendControlPacket(AdspConstants.MSG_HELLO, helloPayload)

            val input = DataInputStream(BufferedInputStream(socket.getInputStream(), 65536))
            val hdrBuf = ByteArray(AdspConstants.HEADER_SIZE)

            input.readFully(hdrBuf)
            val ackHeader = AdspProtocol.parseHeader(hdrBuf)
                ?: throw IllegalStateException("Malformed HELLO_ACK header")
            if (!ackHeader.isValid() || ackHeader.msgType != AdspConstants.MSG_HELLO_ACK) {
                throw IllegalStateException("Unexpected handshake response: ${ackHeader.msgType}")
            }

            val ackPayload = ByteArray(ackHeader.payloadLen)
            if (ackPayload.isNotEmpty()) input.readFully(ackPayload)
            val ack = AdspProtocol.parseHelloAck(ackPayload)
                ?: throw IllegalStateException("Invalid HELLO_ACK payload")
            if (ack.status != 0 || ack.sessionToken == 0) {
                throw IllegalStateException("Host rejected session (status=${ack.status})")
            }

            sessionToken = ack.sessionToken
            val usingTcpVideo = ack.videoOverTcp || effectiveTransportPref == AdspConstants.TRANSPORT_TCP
            callbacks.onHandshakeAccepted(ack, usingTcpVideo)

            if (!usingTcpVideo) {
                startUdpVideoReceiver(params.hostIp, ack.udpVideoPort, ack.sessionToken)
            }
            startPingLoop()

            while (coroutineContext.isActive) {
                input.readFully(hdrBuf)
                val header = AdspProtocol.parseHeader(hdrBuf) ?: break
                if (!header.isValid()) break
                val payload = ByteArray(header.payloadLen)
                if (payload.isNotEmpty()) {
                    input.readFully(payload)
                }

                when (header.msgType) {
                    AdspConstants.MSG_VIDEO_FRAME_TCP -> {
                        val av = AdspProtocol.parseAvPacketHeader(payload)
                        if (av != null && payload.size > AdspConstants.AV_HEADER_SIZE) {
                            val data = payload.copyOfRange(AdspConstants.AV_HEADER_SIZE, payload.size)
                            lastGoodFrameId = av.frameId
                            callbacks.onVideoFrameReceived(
                                AssembledVideoFrame(
                                    frameId = av.frameId,
                                    ptsUs = av.ptsUs,
                                    codec = av.codec,
                                    isIdr = av.isIdr,
                                    containsCsd = av.containsCsd,
                                    annexB = data
                                )
                            )
                        }
                    }
                    AdspConstants.MSG_PING -> {
                        if (payload.size >= 8) {
                            sendControlPacket(AdspConstants.MSG_PONG, payload.copyOfRange(0, 8))
                        }
                    }
                    AdspConstants.MSG_PONG -> {
                        if (payload.size >= 8) {
                            val sentNs = java.nio.ByteBuffer.wrap(payload, 0, 8)
                                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                                .long
                            val rttMs = (System.nanoTime() - sentNs).coerceAtLeast(0L) / 1_000_000.0
                            callbacks.onRttUpdated(rttMs)
                        }
                    }
                    AdspConstants.MSG_DISCONNECT -> {
                        disconnectReason = "Host closed session"
                        break
                    }
                }
            }
        } catch (ex: Exception) {
            disconnectReason = ex.message ?: "Connection lost"
        } finally {
            closeSockets()
            callbacks.onDisconnected(disconnectReason)
        }
    }

    private fun startUdpVideoReceiver(hostIp: String, udpPort: Int, token: Int) {
        udpJob?.cancel()
        udpJob = scope.launch {
            val reassembler = UdpFrameReassembler(maxPendingFrames = 14)
            var prevDropped = 0L
            try {
                val dgram = DatagramSocket().apply {
                    receiveBufferSize = 8 * 1024 * 1024
                    soTimeout = 1000
                }
                udpSocket = dgram
                val hostAddr = InetAddress.getByName(hostIp)
                val reg = AdspProtocol.buildUdpRegisterPacket(token)
                repeat(4) {
                    dgram.send(DatagramPacket(reg, reg.size, hostAddr, udpPort))
                    delay(15)
                }

                val rxBuf = ByteArray(2048)
                val pkt = DatagramPacket(rxBuf, rxBuf.size)
                var lastRegMs = System.currentTimeMillis()
                while (isActive) {
                    try {
                        dgram.receive(pkt)
                        val assembled = reassembler.pushPacket(token, pkt.data, pkt.length)
                        val curDropped = reassembler.droppedFrames
                        if (curDropped > prevDropped) {
                            udpDroppedWindow += (curDropped - prevDropped)
                            prevDropped = curDropped
                            val now = System.currentTimeMillis()
                            if (now - lastLossIdrReqMs >= 250L) {
                                lastLossIdrReqMs = now
                                requestKeyframe(1)
                            }
                        }
                        if (assembled != null) {
                            udpDeliveredWindow++
                            lastGoodFrameId = assembled.frameId
                            callbacks.onVideoFrameReceived(assembled)
                        }
                    } catch (_: Exception) {
                        val now = System.currentTimeMillis()
                        if (now - lastRegMs > 2000L) {
                            lastRegMs = now
                            try {
                                dgram.send(DatagramPacket(reg, reg.size, hostAddr, udpPort))
                            } catch (_: Exception) {
                            }
                        }
                    }
                }
            } catch (_: Exception) {
            }
        }
    }

    private fun startPingLoop() {
        pingJob?.cancel()
        pingJob = scope.launch {
            while (isActive) {
                delay(1500)
                sendControlPacket(AdspConstants.MSG_PING, AdspProtocol.buildLongPayload(System.nanoTime()))
            }
        }
    }

    fun sendTouchBatch(contacts: List<TouchContactData>) {
        if (contacts.isEmpty() || sessionToken == 0) return
        val payload = AdspProtocol.buildTouchBatchPayload(contacts)
        scope.launch {
            sendControlPacket(AdspConstants.MSG_INPUT_TOUCH_BATCH, payload)
        }
    }

    fun sendPenEvent(pen: PenEventData) {
        if (sessionToken == 0) return
        val payload = AdspProtocol.buildPenPayload(pen)
        scope.launch {
            sendControlPacket(AdspConstants.MSG_INPUT_PEN, payload)
        }
    }

    fun sendMouseEvent(mouse: MouseEventData) {
        if (sessionToken == 0) return
        val payload = AdspProtocol.buildMousePayload(mouse)
        scope.launch {
            sendControlPacket(AdspConstants.MSG_INPUT_MOUSE, payload)
        }
    }

    fun sendDisplayConfigUpdate(
        width: Int,
        height: Int,
        refreshHz: Int,
        bitrateKbps: Int,
        codec: Int,
        scaleMode: Int,
        lowLatency: Boolean
    ) {
        if (sessionToken == 0) return
        val payload = AdspProtocol.buildDisplayConfigReq(
            width, height, refreshHz, bitrateKbps, codec, scaleMode, lowLatency
        )
        scope.launch {
            sendControlPacket(AdspConstants.MSG_DISPLAY_CONFIG_REQ, payload)
        }
    }

    fun sendFrameAck(frameId: Int, decodeUs: Int) {
        if (sessionToken == 0) return
        val dropped = udpDroppedWindow
        val delivered = udpDeliveredWindow
        udpDroppedWindow = 0L
        udpDeliveredWindow = 0L
        val total = dropped + delivered
        val lossPermille = if (total > 0L) ((dropped * 1000L) / total).toInt().coerceIn(0, 1000) else 0
        val payload = AdspProtocol.buildFrameAckPayload(
            frameId = frameId,
            recvTimeUs = System.nanoTime() / 1000L,
            decodeUs = decodeUs,
            lossPermille = lossPermille,
            jitterMs = 0
        )
        scope.launch {
            sendControlPacket(AdspConstants.MSG_FRAME_ACK, payload)
        }
    }

    fun requestKeyframe(reason: Int = 1) {
        if (sessionToken == 0) return
        val payload = AdspProtocol.buildKeyframeReqPayload(lastGoodFrameId, reason)
        scope.launch {
            sendControlPacket(AdspConstants.MSG_KEYFRAME_REQ, payload)
        }
    }

    private fun sendControlPacket(msgType: Byte, payload: ByteArray) {
        val pkt = AdspProtocol.buildControlPacket(msgType, seq.getAndIncrement(), payload)
        synchronized(tcpLock) {
            try {
                tcpOut?.write(pkt)
                tcpOut?.flush()
            } catch (_: Exception) {
            }
        }
    }

    fun disconnect(sendBye: Boolean = true) {
        if (sendBye && sessionToken != 0) {
            try {
                val pkt = AdspProtocol.buildControlPacket(AdspConstants.MSG_DISCONNECT, seq.getAndIncrement())
                synchronized(tcpLock) {
                    tcpOut?.write(pkt)
                    tcpOut?.flush()
                }
            } catch (_: Exception) {
            }
        }
        controlJob?.cancel()
        pingJob?.cancel()
        udpJob?.cancel()
        closeSockets()
    }

    private fun closeSockets() {
        sessionToken = 0
        try {
            udpSocket?.close()
        } catch (_: Exception) {
        }
        udpSocket = null
        try {
            tcpSocket?.close()
        } catch (_: Exception) {
        }
        tcpSocket = null
        tcpOut = null
    }

    fun destroy() {
        disconnect(sendBye = true)
        scope.cancel()
    }
}
