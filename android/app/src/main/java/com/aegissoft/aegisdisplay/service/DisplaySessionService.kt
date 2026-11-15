package com.aegissoft.aegisdisplay.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.provider.Settings
import androidx.core.app.NotificationCompat
import androidx.core.net.toUri
import com.aegissoft.aegisdisplay.MainActivity
import com.aegissoft.aegisdisplay.R

class DisplaySessionService : Service() {

    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            sendBroadcast(Intent(ACTION_DISCONNECT_BROADCAST).setPackage(packageName))
            stopSelfClean()
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_TOGGLE_HUD) {
            sendBroadcast(Intent(ACTION_TOGGLE_HUD_BROADCAST).setPackage(packageName))
            return START_STICKY
        }

        val hostName = intent?.getStringExtra(EXTRA_HOST_NAME) ?: "Windows"
        val mode = intent?.getStringExtra(EXTRA_TRANSPORT) ?: "ADSP v1"
        ensureChannel()
        val notif = buildNotification(hostName, mode)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val fgsType = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
            } else {
                0
            }
            startForeground(NOTIFICATION_ID, notif, fgsType)
        } else {
            startForeground(NOTIFICATION_ID, notif)
        }

        acquireLocks()
        return START_STICKY
    }

    private fun acquireLocks() {
        if (wakeLock == null) {
            val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "aegisDisplay:StreamWake").apply {
                setReferenceCounted(false)
                acquire()
            }
        }
        if (wifiLock == null) {
            val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            val lockMode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                WifiManager.WIFI_MODE_FULL_LOW_LATENCY
            } else {
                @Suppress("DEPRECATION")
                WifiManager.WIFI_MODE_FULL_HIGH_PERF
            }
            wifiLock = wm.createWifiLock(lockMode, "aegisDisplay:LowLatencyWifi").apply {
                setReferenceCounted(false)
                acquire()
            }
        }
    }

    private fun stopSelfClean() {
        try {
            if (wakeLock?.isHeld == true) wakeLock?.release()
        } catch (_: Exception) {
        }
        wakeLock = null
        try {
            if (wifiLock?.isHeld == true) wifiLock?.release()
        } catch (_: Exception) {
        }
        wifiLock = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        try {
            if (wakeLock?.isHeld == true) wakeLock?.release()
        } catch (_: Exception) {
        }
        try {
            if (wifiLock?.isHeld == true) wifiLock?.release()
        } catch (_: Exception) {
        }
        super.onDestroy()
    }

    private fun ensureChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(NotificationManager::class.java)
            if (nm.getNotificationChannel(CHANNEL_ID) == null) {
                val ch = NotificationChannel(
                    CHANNEL_ID,
                    getString(R.string.notif_channel_name),
                    NotificationManager.IMPORTANCE_LOW
                ).apply {
                    description = getString(R.string.notif_channel_desc)
                    setShowBadge(false)
                }
                nm.createNotificationChannel(ch)
            }
        }
    }

    private fun buildNotification(hostName: String, mode: String): Notification {
        val openIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val openPi = PendingIntent.getActivity(
            this, 1, openIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val stopIntent = Intent(this, DisplaySessionService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPi = PendingIntent.getService(
            this, 2, stopIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val toggleHudIntent = Intent(this, DisplaySessionService::class.java).apply {
            action = ACTION_TOGGLE_HUD
        }
        val toggleHudPi = PendingIntent.getService(
            this, 3, toggleHudIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_aegisdisplay)
            .setContentTitle(getString(R.string.notif_title))
            .setContentText(getString(R.string.notif_body, hostName, mode))
            .setOngoing(true)
            .setContentIntent(openPi)
            .addAction(0, getString(R.string.notif_action_toggle_hud), toggleHudPi)
            .addAction(0, getString(R.string.notif_action_disconnect), stopPi)
            .build()
    }

    companion object {
        const val CHANNEL_ID = "aegis_display_stream"
        const val NOTIFICATION_ID = 4101
        const val EXTRA_HOST_NAME = "extra_host_name"
        const val EXTRA_TRANSPORT = "extra_transport"
        const val ACTION_STOP = "com.aegissoft.aegisdisplay.ACTION_STOP_STREAM"
        const val ACTION_DISCONNECT_BROADCAST = "com.aegissoft.aegisdisplay.DISCONNECT_BROADCAST"
        const val ACTION_TOGGLE_HUD = "com.aegissoft.aegisdisplay.ACTION_TOGGLE_HUD"
        const val ACTION_TOGGLE_HUD_BROADCAST = "com.aegissoft.aegisdisplay.TOGGLE_HUD_BROADCAST"

        fun isBatteryOptimizationExempt(context: Context): Boolean {
            val pm = context.getSystemService(Context.POWER_SERVICE) as PowerManager
            return pm.isIgnoringBatteryOptimizations(context.packageName)
        }

        fun buildBatteryExemptionIntent(context: Context): Intent =
            Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                data = "package:${context.packageName}".toUri()
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }

        fun start(context: Context, hostName: String, transport: String) {
            val i = Intent(context, DisplaySessionService::class.java).apply {
                putExtra(EXTRA_HOST_NAME, hostName)
                putExtra(EXTRA_TRANSPORT, transport)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(i)
            } else {
                context.startService(i)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, DisplaySessionService::class.java))
        }
    }
}
