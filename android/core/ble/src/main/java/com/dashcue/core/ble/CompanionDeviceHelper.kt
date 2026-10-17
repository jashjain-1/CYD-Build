package com.dashcue.core.ble

import android.bluetooth.le.ScanFilter
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.os.Build
import android.os.ParcelUuid
import com.dashcue.core.common.DashCueLog

class CompanionDeviceHelper(private val context: Context) {

    companion object {
        private const val TAG = "CompanionDeviceHelper"
    }

    private val cdm: CompanionDeviceManager? by lazy {
        context.getSystemService(Context.COMPANION_DEVICE_SERVICE) as? CompanionDeviceManager
    }

    fun isCdmAvailable(): Boolean = cdm != null

    /** MAC address of the first active CDM association, or null. */
    fun lookupAssociatedDeviceAddress(): String? {
        val manager = cdm ?: return null
        val address: String? = try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                manager.myAssociations
                    .firstNotNullOfOrNull { assoc -> assoc.deviceMacAddress?.toString() }
            } else {
                @Suppress("DEPRECATION", "UNCHECKED_CAST")
                val legacy = manager.associations as? List<String>
                legacy?.firstOrNull()
            }
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Association lookup failed", e)
            null
        }
        return address?.uppercase()
    }

    fun associate(callback: Callback) {
        val manager = cdm ?: run {
            callback.onFailure("CompanionDeviceManager unavailable on this device")
            return
        }

        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(BleConstants.SERVICE_UUID))
            .build()

        val deviceFilter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(scanFilter)
            .build()

        val request = AssociationRequest.Builder()
            .addDeviceFilter(deviceFilter)
            .setSingleDevice(true)
            .build()

        try {
        manager.associate(
            request,
            object : CompanionDeviceManager.Callback() {
                @Deprecated("Deprecated in Java")
                override fun onDeviceFound(chooserLauncher: IntentSender) {
                    try {
                        callback.onAssociationPending(chooserLauncher)
                    } catch (e: Exception) {
                        DashCueLog.e(TAG, "Failed to launch device chooser", e)
                        callback.onFailure("Failed to launch chooser: ${e.message}")
                    }
                }

                override fun onAssociationPending(intentSender: IntentSender) {
                    onDeviceFound(intentSender)
                }

                override fun onFailure(error: CharSequence?) {
                    DashCueLog.w(TAG, "CDM Association failed: $error")
                    callback.onFailure(error?.toString() ?: "Unknown CDM error")
                }
            },
            null
        )
        } catch (e: SecurityException) {
            DashCueLog.w(TAG, "CDM permission denied", e)
            callback.onFailure("Nearby device permission is required. Grant it in app settings and retry.")
        } catch (e: IllegalStateException) {
            DashCueLog.w(TAG, "CDM association unavailable", e)
            callback.onFailure("Device pairing is currently unavailable. Check Bluetooth and retry.")
        }
    }

    fun startObservingPresence(deviceAddress: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                cdm?.startObservingDevicePresence(deviceAddress)
                DashCueLog.i(TAG, "Observing presence for $deviceAddress")
            } catch (e: Exception) {
                DashCueLog.w(TAG, "Could not start observing presence for $deviceAddress", e)
            }
        }
    }

    fun stopObservingPresence(deviceAddress: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                cdm?.stopObservingDevicePresence(deviceAddress)
            } catch (e: Exception) {
                DashCueLog.w(TAG, "Could not stop observing presence for $deviceAddress", e)
            }
        }
    }

    interface Callback {
        fun onAssociationPending(chooserLauncher: IntentSender)
        fun onFailure(error: String)
    }
}
