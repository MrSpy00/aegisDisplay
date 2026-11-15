package com.aegissoft.aegisdisplay.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.view.Surface
import com.aegissoft.aegisdisplay.protocol.AdspConstants
import com.aegissoft.aegisdisplay.protocol.AssembledVideoFrame
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class LowLatencyVideoDecoder(
    private val onNeedKeyframe: () -> Unit,
    private val onFrameRendered: (fps: Double) -> Unit
) {
    private var codec: MediaCodec? = null
    private var codecThread: HandlerThread? = null
    private var codecHandler: Handler? = null
    private var currentSurface: Surface? = null
    private var width: Int = 0
    private var height: Int = 0
    private var activeCodecType: Int = AdspConstants.CODEC_NONE
    @Volatile
    private var awaitingIdr: Boolean = true
    @Volatile
    private var lastSubmittedFrameId: Int = -1
    private var lastKeyframeReqMs: Long = 0L

    private val inputIndices = ArrayBlockingQueue<Int>(32)
    private val frameCounter = AtomicInteger(0)
    private var lastFpsTimestampMs = System.currentTimeMillis()

    private fun throttledNeedKeyframe() {
        val now = System.currentTimeMillis()
        if (now - lastKeyframeReqMs >= 250L) {
            lastKeyframeReqMs = now
            onNeedKeyframe()
        }
    }

    @Synchronized
    fun configure(surface: Surface, videoWidth: Int, videoHeight: Int, codecType: Int) {
        release()
        currentSurface = surface
        width = videoWidth.coerceAtLeast(320)
        height = videoHeight.coerceAtLeast(240)
        activeCodecType = codecType
        awaitingIdr = true

        val mime = if (codecType == AdspConstants.CODEC_HEVC) {
            MediaFormat.MIMETYPE_VIDEO_HEVC
        } else {
            MediaFormat.MIMETYPE_VIDEO_AVC
        }

        val thread = HandlerThread("aegis-mediacodec", Process.THREAD_PRIORITY_URGENT_DISPLAY).apply {
            start()
        }
        codecThread = thread
        val handler = Handler(thread.looper)
        codecHandler = handler

        val hwName = findHardwareDecoderName(mime)
        val mc = if (hwName != null) {
            MediaCodec.createByCodecName(hwName)
        } else {
            MediaCodec.createDecoderByType(mime)
        }

        val format = MediaFormat.createVideoFormat(mime, width, height).apply {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
            }
            setInteger(MediaFormat.KEY_OPERATING_RATE, Short.MAX_VALUE.toInt())
            setInteger(MediaFormat.KEY_PRIORITY, 0) // Real-time
            // Honor / Huawei Kirin low-latency decoder hint (observed in Faz 1 RE)
            setInteger("vendor.hisi-ext-low-latency-video-dec.video-scene-for-low-latency-req", 1)
            // Qualcomm Snapdragon low-latency hint
            setInteger("vendor.qti-ext-dec-low-latency.enable", 1)
        }

        mc.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
                if (!inputIndices.offer(index)) {
                    try {
                        codec.queueInputBuffer(index, 0, 0, 0L, 0)
                    } catch (_: Exception) {
                    }
                }
            }

            override fun onOutputBufferAvailable(
                codec: MediaCodec,
                index: Int,
                info: MediaCodec.BufferInfo
            ) {
                try {
                    val render = (info.size > 0) && ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0)
                    if (render) {
                        codec.releaseOutputBuffer(index, true)
                        trackFps()
                    } else {
                        codec.releaseOutputBuffer(index, false)
                    }
                } catch (_: Exception) {
                }
            }

            override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                awaitingIdr = true
                onNeedKeyframe()
            }

            override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                // Surface renders automatically
            }
        }, handler)

        mc.configure(format, surface, null, 0)
        mc.start()
        codec = mc
    }

    fun submitFrame(frame: AssembledVideoFrame) {
        val mc = codec ?: return
        if (awaitingIdr) {
            if (!frame.isIdr) {
                throttledNeedKeyframe()
                return
            }
            awaitingIdr = false
        } else if (!frame.isIdr && lastSubmittedFrameId >= 0 && frame.frameId != lastSubmittedFrameId + 1) {
            // Frame gap detected (e.g. lost UDP packet): wait for clean IDR instead of decoding corrupted P-frame!
            awaitingIdr = true
            lastSubmittedFrameId = -1
            throttledNeedKeyframe()
            return
        }

        val waitMs = if (frame.isIdr) 18L else 5L
        val inputIdx = try {
            inputIndices.poll(waitMs, TimeUnit.MILLISECONDS)
        } catch (_: InterruptedException) {
            null
        }
        if (inputIdx == null) {
            awaitingIdr = true
            lastSubmittedFrameId = -1
            throttledNeedKeyframe()
            return
        }

        try {
            val buf = mc.getInputBuffer(inputIdx)
            if (buf == null) {
                mc.queueInputBuffer(inputIdx, 0, 0, 0L, 0)
                return
            }
            buf.clear()
            if (buf.capacity() < frame.annexB.size) {
                mc.queueInputBuffer(inputIdx, 0, 0, 0L, 0)
                awaitingIdr = true
                lastSubmittedFrameId = -1
                throttledNeedKeyframe()
                return
            }
            buf.put(frame.annexB)
            val flags = if (frame.isIdr) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0
            mc.queueInputBuffer(inputIdx, 0, frame.annexB.size, frame.ptsUs, flags)
            lastSubmittedFrameId = frame.frameId
        } catch (_: Exception) {
            awaitingIdr = true
            lastSubmittedFrameId = -1
            throttledNeedKeyframe()
        }
    }

    private fun trackFps() {
        val count = frameCounter.incrementAndGet()
        val now = System.currentTimeMillis()
        val elapsed = now - lastFpsTimestampMs
        if (elapsed >= 500L) {
            val fps = (count * 1000.0) / elapsed.coerceAtLeast(1L)
            frameCounter.set(0)
            lastFpsTimestampMs = now
            onFrameRendered(fps)
        }
    }

    @Synchronized
    fun release() {
        inputIndices.clear()
        try {
            codec?.stop()
        } catch (_: Exception) {
        }
        try {
            codec?.release()
        } catch (_: Exception) {
        }
        codec = null
        try {
            codecThread?.quitSafely()
        } catch (_: Exception) {
        }
        codecThread = null
        codecHandler = null
        currentSurface = null
        awaitingIdr = true
        lastSubmittedFrameId = -1
        lastKeyframeReqMs = 0L
    }

    private fun findHardwareDecoderName(mime: String): String? {
        val list = MediaCodecList(MediaCodecList.REGULAR_CODECS)
        for (info in list.codecInfos) {
            if (info.isEncoder) continue
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                if (!info.isHardwareAccelerated || info.isSoftwareOnly) continue
            } else {
                val n = info.name.lowercase()
                if (n.startsWith("omx.google.") || n.startsWith("c2.android.")) continue
            }
            if (info.supportedTypes.any { it.equals(mime, ignoreCase = true) }) {
                return info.name
            }
        }
        return null
    }
}
