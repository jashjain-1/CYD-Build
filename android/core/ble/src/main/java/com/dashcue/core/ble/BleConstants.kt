package com.dashcue.core.ble

import java.util.UUID

object BleConstants {
    // DashCue HUD GATT Service & Characteristic UUIDs (frozen per protocol/uuids.md)
    val SERVICE_UUID: UUID = UUID.fromString("7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10")
    val RX_CHAR_UUID: UUID = UUID.fromString("7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10") // Phone -> HUD (Write)
    val TX_CHAR_UUID: UUID = UUID.fromString("7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10") // HUD -> Phone (Notify)
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    // Device Information Service (DIS)
    val DIS_SERVICE_UUID: UUID = UUID.fromString("0000180a-0000-1000-8000-00805f9b34fb")
    val DIS_FW_REV_UUID: UUID = UUID.fromString("00002a26-0000-1000-8000-00805f9b34fb")
    val DIS_HW_REV_UUID: UUID = UUID.fromString("00002a27-0000-1000-8000-00805f9b34fb")
    val DIS_SERIAL_UUID: UUID = UUID.fromString("00002a25-0000-1000-8000-00805f9b34fb")

    const val DEFAULT_ATT_MTU = 23
    const val TARGET_ATT_MTU = 247

    const val HEARTBEAT_INTERVAL_MS = 1000L
    const val LINK_LOSS_TIMEOUT_MS = 20_000L // Matches STALE_PHONE_MS on HUD firmware (20s)
    const val STABILITY_RESET_TIME_MS = 30_000L // 30s connected before backoff resets
}
