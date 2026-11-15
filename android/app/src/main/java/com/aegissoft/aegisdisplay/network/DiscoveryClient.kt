package com.aegissoft.aegisdisplay.network

import com.aegissoft.aegisdisplay.protocol.AdspConstants
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.InterfaceAddress
import java.net.NetworkInterface
import java.nio.charset.StandardCharsets

data class DiscoveredHost(
    val hostId: String,
    val hostname: String,
    val ipAddress: String,
    val tcpPort: Int,
    val udpPort: Int,
    val gpuName: String,
    val supportsHevc: Boolean,
    val lastSeenMs: Long
)

class DiscoveryClient {

    suspend fun listenAndProbe(
        durationMs: Long = 2200L,
        onHostFound: (DiscoveredHost) -> Unit
    ) = withContext(Dispatchers.IO) {
        var socket: DatagramSocket? = null
        try {
            socket = DatagramSocket(null).apply {
                reuseAddress = true
                broadcast = true
                bind(InetSocketAddress(AdspConstants.DEFAULT_DISCOVERY_PORT))
                soTimeout = 350
            }

            sendProbeBurst(socket)

            val deadline = System.currentTimeMillis() + durationMs
            val buf = ByteArray(1024)
            val pkt = DatagramPacket(buf, buf.size)
            while (isActive && System.currentTimeMillis() < deadline) {
                try {
                    socket.receive(pkt)
                    val text = String(pkt.data, pkt.offset, pkt.length, StandardCharsets.UTF_8)
                    val hostIp = pkt.address?.hostAddress ?: continue
                    parseBeaconJson(text, hostIp)?.let(onHostFound)
                } catch (_: Exception) {
                    // Timeout tick
                }
            }
        } catch (_: Exception) {
            // Port busy or socket permission issue: try ephemeral probe socket instead
            runEphemeralProbe(durationMs, onHostFound)
        } finally {
            try {
                socket?.close()
            } catch (_: Exception) {
            }
        }
    }

    private fun runEphemeralProbe(durationMs: Long, onHostFound: (DiscoveredHost) -> Unit) {
        var sock: DatagramSocket? = null
        try {
            sock = DatagramSocket().apply {
                broadcast = true
                soTimeout = 350
            }
            sendProbeBurst(sock)
            val deadline = System.currentTimeMillis() + durationMs
            val buf = ByteArray(1024)
            val pkt = DatagramPacket(buf, buf.size)
            while (System.currentTimeMillis() < deadline) {
                try {
                    sock.receive(pkt)
                    val text = String(pkt.data, pkt.offset, pkt.length, StandardCharsets.UTF_8)
                    val hostIp = pkt.address?.hostAddress ?: continue
                    parseBeaconJson(text, hostIp)?.let(onHostFound)
                } catch (_: Exception) {
                }
            }
        } catch (_: Exception) {
        } finally {
            try {
                sock?.close()
            } catch (_: Exception) {
            }
        }
    }

    private fun sendProbeBurst(socket: DatagramSocket) {
        val probe = "ADSP_DISC".toByteArray(StandardCharsets.UTF_8)
        val targets = linkedSetOf<InetAddress>()
        try {
            targets.add(InetAddress.getByName("255.255.255.255"))
            val ifaces = NetworkInterface.getNetworkInterfaces()
            while (ifaces.hasMoreElements()) {
                val nif = ifaces.nextElement()
                if (!nif.isUp || nif.isLoopback) continue
                for (ia: InterfaceAddress in nif.interfaceAddresses) {
                    ia.broadcast?.let { targets.add(it) }
                }
            }
        } catch (_: Exception) {
        }
        for (addr in targets) {
            try {
                socket.send(DatagramPacket(probe, probe.size, addr, AdspConstants.DEFAULT_DISCOVERY_PORT))
            } catch (_: Exception) {
            }
        }
    }

    companion object {
        fun parseBeaconJson(jsonText: String, sourceIp: String): DiscoveredHost? {
            return try {
                val obj = JSONObject(jsonText)
                if (obj.optString("magic") != "ADSP1") return null
                val codecsArr = obj.optJSONArray("codecs")
                var hasHevc = false
                if (codecsArr != null) {
                    for (i in 0 until codecsArr.length()) {
                        if (codecsArr.optString(i).equals("HEVC", ignoreCase = true)) {
                            hasHevc = true
                        }
                    }
                }
                DiscoveredHost(
                    hostId = obj.optString("hostId", sourceIp),
                    hostname = obj.optString("hostname", sourceIp),
                    ipAddress = sourceIp,
                    tcpPort = obj.optInt("tcpPort", AdspConstants.DEFAULT_CONTROL_PORT),
                    udpPort = obj.optInt("udpPort", AdspConstants.DEFAULT_VIDEO_PORT),
                    gpuName = obj.optString("gpu", "GPU"),
                    supportsHevc = hasHevc,
                    lastSeenMs = System.currentTimeMillis()
                )
            } catch (_: Exception) {
                null
            }
        }
    }
}
