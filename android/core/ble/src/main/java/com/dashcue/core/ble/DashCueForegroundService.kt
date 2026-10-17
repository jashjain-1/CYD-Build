package com.dashcue.core.ble

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.dashcue.core.common.DashCueLog
import kotlinx.coroutines.*

class DashCueForegroundService : Service() {

    companion object {
        private const val TAG = "ForegroundService"
        const val CHANNEL_ID = "dashcue_foreground_channel"
        const val NOTIFICATION_ID = 2001

        const val ACTION_START = "com.dashcue.action.START"
        const val ACTION_STOP = "com.dashcue.action.STOP"
        const val EXTRA_DEVICE_ADDRESS = "extra_device_address"

        fun startService(context: Context, deviceAddress: String) {
            val intent = Intent(context, DashCueForegroundService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_DEVICE_ADDRESS, deviceAddress)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stopService(context: Context) {
            val intent = Intent(context, DashCueForegroundService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }
    }

    private val binder = LocalBinder()
    private val serviceScope = CoroutineScope(Dispatchers.Main + SupervisorJob())
    var bleManager: DashCueBleManager? = null
        private set

    inner class LocalBinder : Binder() {
        fun getService(): DashCueForegroundService = this@DashCueForegroundService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        bleManager = DashCueBleManager(applicationContext, serviceScope)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopForegroundInternal()
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_START -> {
                val address = intent.getStringExtra(EXTRA_DEVICE_ADDRESS)
                startForegroundWithNotification("Connecting to DashCue HUD...")
                if (!address.isNullOrBlank()) {
                    bleManager?.startConnecting(address)
                }
            }
        }
        return START_STICKY
    }

    private fun startForegroundWithNotification(statusText: String) {
        val notification = buildNotification(statusText)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val types = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            } else {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            }
            startForeground(NOTIFICATION_ID, notification, types)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    fun updateStatusText(statusText: String) {
        val notification = buildNotification(statusText)
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, notification)
    }

    private fun buildNotification(statusText: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("DashCue HUD Active")
            .setContentText(statusText)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "DashCue HUD Background Service",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Keeps connection to DashCue HUD alive for navigation and media sync"
                setShowBadge(false)
            }
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    private fun stopForegroundInternal() {
        bleManager?.disconnect()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopForegroundInternal()
        serviceScope.cancel()
    }
}
