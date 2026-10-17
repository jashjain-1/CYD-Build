package com.dashcue.core.ble

import org.junit.Assert.*
import org.junit.Test
import java.util.Random

class ReconnectionStateMachineTest {

    @Test
    fun testInitialState() {
        val sm = ReconnectionStateMachine()
        assertEquals(ReconnectionStateMachine.State.DISCONNECTED, sm.currentState)
        assertEquals(0, sm.backoffIndex)
        assertEquals(ReconnectionStateMachine.TerminalReason.NONE, sm.terminalReason)
    }

    @Test
    fun testLifecycleTransitions() {
        val sm = ReconnectionStateMachine()
        sm.onScanStarted()
        assertEquals(ReconnectionStateMachine.State.SCANNING, sm.currentState)

        sm.onConnecting()
        assertEquals(ReconnectionStateMachine.State.CONNECTING, sm.currentState)

        sm.onServicesDiscovered()
        assertEquals(ReconnectionStateMachine.State.DISCOVERING_SERVICES, sm.currentState)

        sm.onNegotiating()
        assertEquals(ReconnectionStateMachine.State.NEGOTIATING, sm.currentState)

        sm.onSynchronising()
        assertEquals(ReconnectionStateMachine.State.SYNCHRONISING, sm.currentState)

        sm.onReady(currentTimeMs = 1000L)
        assertEquals(ReconnectionStateMachine.State.READY, sm.currentState)
    }

    @Test
    fun testExponentialBackoffProgressionAndCap() {
        val deterministicRandom = object : Random() {
            override fun nextDouble(): Double = 0.5 // Jitter = 0
        }
        val sm = ReconnectionStateMachine(random = deterministicRandom)

        // Step 0: Base 1000ms
        assertEquals(1000L, sm.getNextDelayMs())
        sm.onDisconnected(currentTimeMs = 1000L)
        assertEquals(1, sm.backoffIndex)

        // Step 1: Base 2000ms
        assertEquals(2000L, sm.getNextDelayMs())
        sm.onDisconnected(currentTimeMs = 2000L)
        assertEquals(2, sm.backoffIndex)

        // Step 2: Base 4000ms
        assertEquals(4000L, sm.getNextDelayMs())
        sm.onDisconnected(currentTimeMs = 3000L)
        assertEquals(3, sm.backoffIndex)

        // Step 3: Base 8000ms
        assertEquals(8000L, sm.getNextDelayMs())
        sm.onDisconnected(currentTimeMs = 4000L)
        assertEquals(4, sm.backoffIndex)

        // Step 4: Base 15000ms
        assertEquals(15000L, sm.getNextDelayMs())
        sm.onDisconnected(currentTimeMs = 5000L)
        assertEquals(5, sm.backoffIndex)

        // Step 5: Base 30000ms (Cap)
        assertEquals(30000L, sm.getNextDelayMs())
        sm.onDisconnected(currentTimeMs = 6000L)
        assertEquals(5, sm.backoffIndex)
        assertEquals(30000L, sm.getNextDelayMs())
    }

    @Test
    fun testJitterBounds() {
        val sm = ReconnectionStateMachine()
        // Test 100 iterations of jitter within +-20% of base delay
        for (i in 0 until 100) {
            val delay = sm.getNextDelayMs()
            assertTrue("Delay $delay should be >= 800", delay >= 800L)
            assertTrue("Delay $delay should be <= 1200", delay <= 1200L)
        }
    }

    @Test
    fun testStabilityResetAfter30SecondsInReady() {
        val sm = ReconnectionStateMachine()
        // Advance to step 3 (8000ms)
        sm.onDisconnected()
        sm.onDisconnected()
        sm.onDisconnected()
        assertEquals(3, sm.backoffIndex)

        // Enter Ready at t = 10,000ms
        sm.onReady(currentTimeMs = 10_000L)

        // Disconnect at t = 45,000ms (35s in Ready >= 30s threshold)
        sm.onDisconnected(currentTimeMs = 45_000L)
        assertEquals("Backoff should reset to 0 after 35s in READY", 0, sm.backoffIndex)
    }

    @Test
    fun testNoResetIfDisconnectedBefore30SecondsInReady() {
        val sm = ReconnectionStateMachine()
        sm.onDisconnected()
        sm.onDisconnected()
        assertEquals(2, sm.backoffIndex)

        // Enter Ready at t = 10,000ms
        sm.onReady(currentTimeMs = 10_000L)

        // Disconnect at t = 20,000ms (10s in Ready < 30s threshold)
        sm.onDisconnected(currentTimeMs = 20_000L)
        assertEquals("Backoff should increase to step 3", 3, sm.backoffIndex)
    }

    @Test
    fun testLinkLossDetectionAt6Seconds() {
        val sm = ReconnectionStateMachine()
        sm.onReady(currentTimeMs = 10_000L)

        // Check at 15s (5s elapsed < 6s threshold)
        assertFalse(sm.checkLinkLoss(currentTimeMs = 15_000L))
        assertEquals(ReconnectionStateMachine.State.READY, sm.currentState)

        // Packet received at 15s resets last packet timer
        sm.onPacketReceived(currentTimeMs = 15_000L)

        // Check at 20s (5s since packet < 6s)
        assertFalse(sm.checkLinkLoss(currentTimeMs = 20_000L))

        // Check at 21.1s (6.1s since last packet >= 6s)
        assertTrue(sm.checkLinkLoss(currentTimeMs = 21_100L))
        assertEquals(ReconnectionStateMachine.State.DISCONNECTED, sm.currentState)
    }

    @Test
    fun testTerminalStates() {
        val sm = ReconnectionStateMachine()
        sm.onTerminalError(ReconnectionStateMachine.TerminalReason.BLUETOOTH_OFF)
        assertEquals(ReconnectionStateMachine.State.TERMINAL_ERROR, sm.currentState)
        assertEquals(ReconnectionStateMachine.TerminalReason.BLUETOOTH_OFF, sm.terminalReason)

        // Normal events should be ignored while in terminal state
        sm.onConnecting()
        assertEquals(ReconnectionStateMachine.State.TERMINAL_ERROR, sm.currentState)

        // Explicit reset clears terminal state
        sm.reset()
        assertEquals(ReconnectionStateMachine.State.DISCONNECTED, sm.currentState)
        assertEquals(ReconnectionStateMachine.TerminalReason.NONE, sm.terminalReason)
    }
}
