package com.dashcue.core.ble

import java.util.Random

/**
 * Reconnection state machine with exponential backoff and jitter (spec §9.6, TC-AN-01).
 *
 * Intervals: 1s, 2s, 4s, 8s, 15s, 30s (+-20% jitter).
 * Reset condition: when in Ready state for >= 30 seconds, counter resets to 1s.
 * Terminal states: BluetoothOff, PermissionRevoked, AuthRejected, VersionMismatch.
 */
class ReconnectionStateMachine(
    private val random: Random = Random(),
    val backoffSchedule: LongArray = longArrayOf(1000L, 2000L, 4000L, 8000L, 15000L, 30000L),
    val jitterRange: Double = 0.20,
    val stabilityThresholdMs: Long = 30_000L,
    val linkLossThresholdMs: Long = BleConstants.LINK_LOSS_TIMEOUT_MS
) {
    enum class State {
        DISCONNECTED,
        SCANNING,
        CONNECTING,
        DISCOVERING_SERVICES,
        NEGOTIATING,
        SYNCHRONISING,
        READY,
        TERMINAL_ERROR
    }

    enum class TerminalReason {
        NONE,
        BLUETOOTH_OFF,
        PERMISSION_REVOKED,
        AUTH_REJECTED,
        VERSION_MISMATCH
    }

    var currentState: State = State.DISCONNECTED
        private set

    var terminalReason: TerminalReason = TerminalReason.NONE
        private set

    var backoffIndex: Int = 0
        private set

    var readyEnteredTimestampMs: Long = 0L
        private set

    var lastPacketReceivedTimestampMs: Long = 0L
        private set

    fun getNextDelayMs(): Long {
        val baseDelay = backoffSchedule[minOf(backoffIndex, backoffSchedule.size - 1)]
        // Jitter uniform in [-jitterRange, +jitterRange]
        val jitter = (random.nextDouble() * 2.0 * jitterRange) - jitterRange
        val delayWithJitter = (baseDelay * (1.0 + jitter)).toLong()
        return maxOf(0L, delayWithJitter)
    }

    fun onScanStarted() {
        if (currentState != State.TERMINAL_ERROR) {
            currentState = State.SCANNING
        }
    }

    fun onConnecting() {
        if (currentState != State.TERMINAL_ERROR) {
            currentState = State.CONNECTING
        }
    }

    fun onServicesDiscovered() {
        if (currentState != State.TERMINAL_ERROR) {
            currentState = State.DISCOVERING_SERVICES
        }
    }

    fun onNegotiating() {
        if (currentState != State.TERMINAL_ERROR) {
            currentState = State.NEGOTIATING
        }
    }

    fun onSynchronising() {
        if (currentState != State.TERMINAL_ERROR) {
            currentState = State.SYNCHRONISING
        }
    }

    fun onReady(currentTimeMs: Long = System.currentTimeMillis()) {
        if (currentState != State.TERMINAL_ERROR) {
            currentState = State.READY
            readyEnteredTimestampMs = currentTimeMs
            lastPacketReceivedTimestampMs = currentTimeMs
        }
    }

    fun onPacketReceived(currentTimeMs: Long = System.currentTimeMillis()) {
        lastPacketReceivedTimestampMs = currentTimeMs
    }

    fun checkLinkLoss(currentTimeMs: Long = System.currentTimeMillis()): Boolean {
        if (currentState == State.READY) {
            if (currentTimeMs - lastPacketReceivedTimestampMs >= linkLossThresholdMs) {
                // Link is dead, trigger disconnect
                onDisconnected(currentTimeMs)
                return true
            }
        }
        return false
    }

    fun onDisconnected(currentTimeMs: Long = System.currentTimeMillis()) {
        if (currentState == State.TERMINAL_ERROR) return

        if (currentState == State.READY) {
            val durationInReady = currentTimeMs - readyEnteredTimestampMs
            if (durationInReady >= stabilityThresholdMs) {
                // Connection was stable >= 30s, reset backoff to index 0 (1s)
                backoffIndex = 0
            } else {
                backoffIndex = minOf(backoffIndex + 1, backoffSchedule.size - 1)
            }
        } else {
            backoffIndex = minOf(backoffIndex + 1, backoffSchedule.size - 1)
        }

        currentState = State.DISCONNECTED
        readyEnteredTimestampMs = 0L
    }

    fun onTerminalError(reason: TerminalReason) {
        currentState = State.TERMINAL_ERROR
        terminalReason = reason
    }

    fun reset() {
        currentState = State.DISCONNECTED
        terminalReason = TerminalReason.NONE
        backoffIndex = 0
        readyEnteredTimestampMs = 0L
        lastPacketReceivedTimestampMs = 0L
    }
}
