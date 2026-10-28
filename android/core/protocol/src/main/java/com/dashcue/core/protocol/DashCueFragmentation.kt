package com.dashcue.core.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * ATT fragmentation and reassembly engine for DashCue protocol (§9.2).
 *
 * Fragment packet layout:
 *   - byte 0: lane (0 = Control, 1 = Bulk)
 *   - byte 1: total_frags (1..255)
 *   - byte 2: frag_index (0..total_frags-1)
 *   - byte 3: credits (credit grant to peer, e.g. 8)
 *   - byte 4..N: chunk payload (size <= MTU - 7)
 */
class DashCueFragmentation(
    val initialCredits: Int = 8,
    val reassemblyTimeoutMs: Long = 2000L
) {
    companion object {
        const val FRAGMENT_HEADER_SIZE = 4
        const val ATT_HEADER_SIZE = 3
        const val MAX_REASSEMBLED_SIZE = DashCueFraming.MAX_FRAME_SIZE // 2068 bytes (16 header + 2048 payload + 4 trailer)
        private const val MAX_CREDITS = 8

        const val LANE_CONTROL = 0
        const val LANE_BULK = 1
        const val NUM_LANES = 2
    }

    // Lane reassembly state
    data class ReassemblyContext(
        var active: Boolean = false,
        var totalFrags: Int = 0,
        var nextExpectedIndex: Int = 0,
        val buffer: MutableList<ByteArray> = mutableListOf(),
        var currentBytes: Int = 0,
        var lastUpdatedMs: Long = 0L
    )

    private val contexts = Array(NUM_LANES) { ReassemblyContext() }

    // Sender credit tracking
    private var outboundCredits = initialCredits.coerceIn(0, MAX_CREDITS)

    fun getOutboundCredits(): Int = outboundCredits

    fun addOutboundCredits(granted: Int) {
        if (granted > 0) outboundCredits = minOf(MAX_CREDITS, outboundCredits + granted)
    }

    /**
     * Splits an enveloped frame into ATT fragments.
     */
    fun fragment(
        frame: ByteArray,
        lane: Int = LANE_CONTROL,
        mtu: Int = 247,
        grantCredits: Int = initialCredits
    ): List<ByteArray> {
        require(lane in 0 until NUM_LANES) { "Invalid lane: $lane" }
        require(mtu >= 23) { "MTU must be at least 23, was $mtu" }
        require(grantCredits in 0..255) { "Credit grant must fit one byte, was $grantCredits" }

        val chunkSize = mtu - 7
        val totalFrags = if (frame.isEmpty()) 1 else (frame.size + chunkSize - 1) / chunkSize
        require(totalFrags <= 255) { "Frame requires $totalFrags fragments, exceeding 255" }

        val fragments = ArrayList<ByteArray>(totalFrags)
        for (i in 0 until totalFrags) {
            val start = i * chunkSize
            val end = minOf(start + chunkSize, frame.size)
            val chunkLen = if (start < frame.size) end - start else 0

            val frag = ByteArray(FRAGMENT_HEADER_SIZE + chunkLen)
            frag[0] = lane.toByte()
            frag[1] = totalFrags.toByte()
            frag[2] = i.toByte()
            frag[3] = grantCredits.toByte()

            if (chunkLen > 0) {
                System.arraycopy(frame, start, frag, FRAGMENT_HEADER_SIZE, chunkLen)
            }
            fragments.add(frag)
        }
        return fragments
    }

    sealed class ReassemblyResult {
        data class Complete(val lane: Int, val frame: ByteArray) : ReassemblyResult()
        data class Progress(val lane: Int, val index: Int, val total: Int) : ReassemblyResult()
        data class Error(val lane: Int, val errorCode: Int, val description: String) : ReassemblyResult()
    }

    /**
     * Ingests an incoming fragment packet and attempts reassembly.
     */
    @Synchronized
    fun onFragmentReceived(
        packet: ByteArray,
        currentTimeMs: Long = System.currentTimeMillis()
    ): ReassemblyResult {
        if (packet.size < FRAGMENT_HEADER_SIZE) {
            return ReassemblyResult.Error(
                LANE_CONTROL,
                DashCueFraming.EC_DECODE,
                "Fragment packet smaller than header ($FRAGMENT_HEADER_SIZE bytes)"
            )
        }

        val lane = packet[0].toInt() and 0xFF
        val totalFrags = packet[1].toInt() and 0xFF
        val fragIndex = packet[2].toInt() and 0xFF
        val creditsGranted = packet[3].toInt() and 0xFF

        if (lane >= NUM_LANES) {
            return ReassemblyResult.Error(lane, DashCueFraming.EC_DECODE, "Invalid lane index: $lane")
        }
        if (totalFrags == 0 || fragIndex >= totalFrags) {
            return ReassemblyResult.Error(
                lane,
                DashCueFraming.EC_DECODE,
                "Invalid fragment index/count: index=$fragIndex count=$totalFrags"
            )
        }

        // Update bounded outbound credit pool only after validating the fragment header.
        if (creditsGranted > 0) addOutboundCredits(creditsGranted)

        val ctx = contexts[lane]

        // Check timeout
        if (ctx.active && (currentTimeMs - ctx.lastUpdatedMs > reassemblyTimeoutMs)) {
            resetLane(lane)
        }

        val chunkLen = packet.size - FRAGMENT_HEADER_SIZE
        val chunkData = ByteArray(chunkLen)
        System.arraycopy(packet, FRAGMENT_HEADER_SIZE, chunkData, 0, chunkLen)

        // Clean lane abort: If lane is already active and a new frame starts with fragIndex == 0,
        // reset the lane cleanly without decode error to start reassembling the new frame.
        if (ctx.active && fragIndex == 0) {
            resetLane(lane)
        }

        // Starting a new reassembly
        if (!ctx.active) {
            if (fragIndex != 0) {
                return ReassemblyResult.Error(
                    lane,
                    DashCueFraming.EC_DECODE,
                    "Expected fragment index 0, got $fragIndex"
                )
            }
            ctx.active = true
            ctx.totalFrags = totalFrags
            ctx.nextExpectedIndex = 0
            ctx.buffer.clear()
            ctx.currentBytes = 0
        }

        // Validate sequence and total count
        if (totalFrags != ctx.totalFrags) {
            resetLane(lane)
            return ReassemblyResult.Error(
                lane,
                DashCueFraming.EC_DECODE,
                "Fragment totalFrags changed mid-transfer: expected ${ctx.totalFrags}, got $totalFrags"
            )
        }

        if (fragIndex != ctx.nextExpectedIndex) {
            resetLane(lane)
            return ReassemblyResult.Error(
                lane,
                DashCueFraming.EC_DECODE,
                "Out-of-order or duplicate fragment: expected ${ctx.nextExpectedIndex}, got $fragIndex"
            )
        }

        if (ctx.currentBytes + chunkLen > MAX_REASSEMBLED_SIZE) {
            resetLane(lane)
            return ReassemblyResult.Error(
                lane,
                DashCueFraming.EC_TOO_LARGE,
                "Reassembled size would exceed $MAX_REASSEMBLED_SIZE bytes"
            )
        }

        ctx.buffer.add(chunkData)
        ctx.currentBytes += chunkLen
        ctx.nextExpectedIndex++
        ctx.lastUpdatedMs = currentTimeMs

        // Check completion
        if (ctx.nextExpectedIndex == ctx.totalFrags) {
            val totalBytes = ctx.currentBytes
            val completeFrame = ByteArray(totalBytes)
            var offset = 0
            for (c in ctx.buffer) {
                System.arraycopy(c, 0, completeFrame, offset, c.size)
                offset += c.size
            }
            resetLane(lane)
            return ReassemblyResult.Complete(lane, completeFrame)
        }

        return ReassemblyResult.Progress(lane, fragIndex, totalFrags)
    }

    fun resetLane(lane: Int) {
        if (lane in 0 until NUM_LANES) {
            val ctx = contexts[lane]
            ctx.active = false
            ctx.totalFrags = 0
            ctx.nextExpectedIndex = 0
            ctx.buffer.clear()
            ctx.currentBytes = 0
            ctx.lastUpdatedMs = 0L
        }
    }

    fun resetAll() {
        for (i in 0 until NUM_LANES) {
            resetLane(i)
        }
    }
}
