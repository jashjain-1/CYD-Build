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
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel

class DashCueBleService : Service() {
    companion object {
        private const val TAG = "DashCueBleService"
        const val CHANNEL_ID = "dashcue_status"
        const val NOTIFICATION_ID = 1001

        const val ACTION_START_FOREGROUND = "com.dashcue.action.START_FOREGROUND"
        const val ACTION_STOP_FOREGROUND = "com.dashcue.action.STOP_FOREGROUND"
        const val EXTRA_DEVICE_ADDRESS = "extra_device_address"
    }

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val binder = LocalBinder()

    lateinit var bleManager: DashCueBleManager
        private set

    inner class LocalBinder : Binder() {
        fun getService(): DashCueBleService = this@DashCueBleService
    }

    override fun onCreate() {
        super.onCreate()
        bleManager = DashCueBleManager(this, serviceScope)
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START_FOREGROUND -> {
                val deviceAddress = intent.getStringExtra(EXTRA_DEVICE_ADDRESS)
                startAsForeground()
                if (!deviceAddress.isNullOrBlank()) {
                    bleManager.startConnecting(deviceAddress)
                }
            }
            ACTION_STOP_FOREGROUND -> {
                bleManager.disconnect()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }
        return START_STICKY
    }

    private fun startAsForeground() {
        val notification = createNotification("Connecting to DashCue HUD...")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun createNotification(contentText: String): Notification {
        val stopIntent = Intent(this, DashCueBleService::class.java).apply {
            action = ACTION_STOP_FOREGROUND
        }
        val stopPendingIntent = PendingIntent.getService(
            this,
            0,
            stopIntent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("DashCue HUD Active")
            .setContentText(contentText)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "End Session", stopPendingIntent)
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "DashCue Status",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Persistent connection and navigation status for DashCue HUD"
            }
            val notificationManager = getSystemService(NotificationManager::class.java)
            notificationManager?.createNotificationChannel(channel)
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        super.onDestroy()
        serviceScope.cancel()
        bleManager.disconnect()
    }
}
