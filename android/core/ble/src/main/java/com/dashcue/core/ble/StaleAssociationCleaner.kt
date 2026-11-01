package com.dashcue.core.ble

import android.Manifest
import android.bluetooth.BluetoothManager
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import com.dashcue.core.common.DashCueLog

/**
 * Removes Companion Device Manager (CDM) associations left behind by older builds.
 *
 * The app used to pair through CDM, which stores an association record in the
 * system. That record outlives the app code: on a phone upgraded from a CDM
 * build, Android keeps trying to complete the association and shows an
 * "Enter pairing code — Pair with DashCue-HUD" dialog even though the current
 * firmware and app have no pairing step at all.
 *
 * The app no longer creates associations; this helper only removes them. It is
 * limited to `disassociate` so it cannot introduce a new pairing flow.
 */
object StaleAssociationCleaner {

    private const val TAG = "StaleAssociationCleaner"

    /**
     * Disassociates every device this app associated with the system.
     *
     * This app only ever associated the DashCue HUD, so "all associations of this
     * app" is exactly the stale set. Safe to call repeatedly; failures are logged
     * and ignored because the phone may have no association or no CDM service.
     */
    fun clean(context: Context) {
        val manager = context.getSystemService(Context.COMPANION_DEVICE_SERVICE)
            as? CompanionDeviceManager ?: return

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                for (association in manager.myAssociations) {
                    val address = association.deviceMacAddress?.toString() ?: continue
                    @Suppress("DEPRECATION")
                    manager.disassociate(address)
                    DashCueLog.i(TAG, "Removed stale companion association $address")
                }
            } else {
                @Suppress("DEPRECATION", "UNCHECKED_CAST")
                val legacy = manager.associations as? List<String> ?: return
                for (address in legacy) {
                    @Suppress("DEPRECATION")
                    manager.disassociate(address)
                    DashCueLog.i(TAG, "Removed stale companion association for $address")
                }
            }
        } catch (e: SecurityException) {
            DashCueLog.w(TAG, "Not allowed to clear companion associations", e)
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Could not clear companion associations", e)
        }
    }

    /**
     * Name of a system Bluetooth bond named like the HUD, or null.
     *
     * A phone that paired with an older firmware keeps that bond in system
     * settings, and Android will pop a legacy "Enter pairing code" dialog for it
     * whenever the app scans. The app cannot delete a system bond, so the UI has
     * to tell the user to remove it by hand.
     */
    fun legacyHudBond(context: Context): String? {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return null
        }
        return try {
            val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
            adapter?.bondedDevices?.firstOrNull { it.name?.contains("DashCue", true) == true }?.name
        } catch (e: SecurityException) {
            null
        }
    }
}
