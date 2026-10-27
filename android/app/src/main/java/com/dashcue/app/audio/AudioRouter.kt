package com.dashcue.app.audio

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build
import com.dashcue.core.common.DashCueLog
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

object AudioRouter {
    private const val TAG = "AudioRouter"

    private val _isBluetoothConnected = MutableStateFlow(false)
    val isBluetoothConnected: StateFlow<Boolean> = _isBluetoothConnected.asStateFlow()

    private val _connectedDeviceName = MutableStateFlow<String?>(null)
    val connectedDeviceName: StateFlow<String?> = _connectedDeviceName.asStateFlow()

    private var registeredCallback: AudioDeviceCallback? = null

    private fun safeLog(level: Int, msg: String, tr: Throwable? = null) {
        try {
            when (level) {
                2 -> DashCueLog.d(TAG, msg)
                4 -> DashCueLog.i(TAG, msg)
                5 -> DashCueLog.w(TAG, msg, tr)
            }
        } catch (_: Throwable) {}
    }

    /**
     * Registers an AudioDeviceCallback with the system AudioManager to monitor
     * Bluetooth audio connections dynamically.
     */
    fun register(context: Context) {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        if (registeredCallback != null) return

        val callback = object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>?) {
                updateDeviceState(audioManager)
            }

            override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>?) {
                updateDeviceState(audioManager)
            }
        }

        try {
            audioManager.registerAudioDeviceCallback(callback, null)
            registeredCallback = callback
            updateDeviceState(audioManager)
        } catch (e: Exception) {
            safeLog(5, "Failed to register audio device callback", e)
        }
    }

    /**
     * Unregisters the AudioDeviceCallback.
     */
    fun unregister(context: Context) {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        val cb = registeredCallback ?: return
        try {
            audioManager.unregisterAudioDeviceCallback(cb)
            registeredCallback = null
        } catch (e: Exception) {
            safeLog(5, "Failed to unregister audio device callback", e)
        }
    }

    /**
     * Scans currently available audio outputs and updates Bluetooth presence StateFlow.
     */
    fun updateDeviceState(audioManager: AudioManager) {
        try {
            val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            val btDevice = devices.firstOrNull { isBluetoothOutput(it) }
            val hasBt = btDevice != null
            _isBluetoothConnected.value = hasBt
            _connectedDeviceName.value = if (hasBt) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    btDevice?.address?.takeIf { it.isNotBlank() } ?: "Bluetooth Vehicle"
                } else {
                    "Bluetooth Vehicle"
                }
            } else {
                null
            }
            safeLog(2, "Audio devices updated: btConnected=$hasBt, device=${_connectedDeviceName.value}")
        } catch (e: Exception) {
            safeLog(5, "Error checking audio devices", e)
        }
    }

    /**
     * Checks if the device is a Bluetooth audio output.
     */
    fun isBluetoothOutput(device: AudioDeviceInfo): Boolean {
        return device.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                device.type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                        (device.type == AudioDeviceInfo.TYPE_BLE_SPEAKER ||
                                device.type == AudioDeviceInfo.TYPE_BLE_HEADSET))
    }

    /**
     * Resolves the target AudioDeviceInfo based on the requested AudioRoute with graceful fallback.
     */
    fun getTargetDevice(context: Context, routeId: Int): AudioDeviceInfo? {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return null
        val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        val route = AudioRoute.fromId(routeId)

        return when (route) {
            AudioRoute.PHONE_SPEAKER -> {
                devices.firstOrNull { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER }
            }
            AudioRoute.BLUETOOTH_VEHICLE -> {
                val btDevice = devices.firstOrNull { isBluetoothOutput(it) }
                // Fall back gracefully to built-in speaker if Bluetooth is not connected
                btDevice ?: devices.firstOrNull { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER }
            }
        }
    }

    /**
     * Routes an active AudioTrack instance to the preferred output device based on the route.
     */
    fun routeAudioTrack(context: Context, track: AudioTrack, routeId: Int): Boolean {
        val targetDevice = getTargetDevice(context, routeId) ?: return false
        return try {
            val success = track.setPreferredDevice(targetDevice)
            safeLog(2, "setPreferredDevice type=${targetDevice.type} success=$success")
            success
        } catch (e: Exception) {
            safeLog(5, "setPreferredDevice failed", e)
            false
        }
    }

    /**
     * Human-readable name of the output the system is currently using for
     * media, for display only. DashCue cannot change another app's route, so
     * this is a readout rather than a control.
     */
    fun activeMediaOutputName(context: Context): String {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
            ?: return "Unknown"
        return try {
            val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            when {
                devices.any { isBluetoothOutput(it) } -> "Bluetooth"
                devices.any { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER } -> "Phone speaker"
                devices.any { it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                    it.type == AudioDeviceInfo.TYPE_USB_HEADSET } -> "USB audio"
                devices.any { it.type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
                    it.type == AudioDeviceInfo.TYPE_WIRED_HEADSET } -> "Wired headset"
                else -> "Unknown"
            }
        } catch (e: Exception) {
            safeLog(5, "activeMediaOutputName failed", e)
            "Unknown"
        }
    }
}
