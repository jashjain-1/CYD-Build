package com.dashcue.core.protocol

import org.junit.Assert.*
import org.junit.Test
import java.io.File

class DashCueFramingTest {

    @Test
    fun testEncodeDecodeRoundtrip() {
        val payload = "Hello DashCue Protocol".toByteArray(Charsets.UTF_8)
        val msgType = 10 // MT_MEDIA_STATE
        val msgId = 1006L
        val flags = 4 // EF_IDEMPOTENT

        val frame = DashCueFraming.encodeFrame(msgType, msgId, flags, payload)
        assertNotNull(frame)
        assertEquals(DashCueFraming.HEADER_SIZE + payload.size + DashCueFraming.TRAILER_SIZE, frame.size)

        val result = DashCueFraming.decodeFrame(frame)
        assertTrue(result is DashCueFraming.DecodeResult.Success)

        val success = result as DashCueFraming.DecodeResult.Success
        assertEquals(DashCueFraming.FRAME_MAGIC, success.header.magic)
        assertEquals(DashCueFraming.FRAME_VERSION, success.header.version)
        assertEquals(flags, success.header.flags)
        assertEquals(msgType, success.header.msgType)
        assertEquals(msgId, success.header.msgId)
        assertEquals(payload.size, success.header.length)
        assertArrayEquals(payload, success.payload)
    }

    @Test
    fun testCorruptCrcRejected() {
        // TC-PR-03: Corrupt CRC32 in envelope -> packet dropped, returns EC_CRC
        val payload = "Important Telemetry".toByteArray(Charsets.UTF_8)
        val frame = DashCueFraming.encodeFrame(50, 1018L, 0, payload)

        // Corrupt trailer byte
        val corruptedFrame = frame.clone()
        corruptedFrame[corruptedFrame.size - 1] = (corruptedFrame[corruptedFrame.size - 1].toInt() xor 0xFF).toByte()

        val result = DashCueFraming.decodeFrame(corruptedFrame)
        assertTrue(result is DashCueFraming.DecodeResult.Error)
        assertEquals(DashCueFraming.EC_CRC, (result as DashCueFraming.DecodeResult.Error).errorCode)
    }

    @Test
    fun testPayloadExceedingLimitRejected() {
        // TC-PR-04: Reject payload > 4096 bytes
        val oversizedPayload = ByteArray(4097)
        try {
            DashCueFraming.encodeFrame(1, 1001L, 0, oversizedPayload)
            fail("Should have thrown IllegalArgumentException for payload > 4096")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message!!.contains("exceeds MAX_PAYLOAD_SIZE"))
        }
    }

    @Test
    fun testInvalidMagicRejected() {
        val payload = "Valid Data".toByteArray(Charsets.UTF_8)
        val frame = DashCueFraming.encodeFrame(1, 1001L, 0, payload)

        // Corrupt magic header (first two bytes)
        val corruptedFrame = frame.clone()
        corruptedFrame[0] = 0x12
        corruptedFrame[1] = 0x34

        val result = DashCueFraming.decodeFrame(corruptedFrame)
        assertTrue(result is DashCueFraming.DecodeResult.Error)
        assertEquals(DashCueFraming.EC_DECODE, (result as DashCueFraming.DecodeResult.Error).errorCode)
    }

    @Test
    fun testInvalidVersionRejected() {
        val payload = "Valid Data".toByteArray(Charsets.UTF_8)
        val frame = DashCueFraming.encodeFrame(1, 1001L, 0, payload)

        // Corrupt version byte (byte 2)
        val corruptedFrame = frame.clone()
        corruptedFrame[2] = 0x99.toByte()

        val result = DashCueFraming.decodeFrame(corruptedFrame)
        assertTrue(result is DashCueFraming.DecodeResult.Error)
        assertEquals(DashCueFraming.EC_VERSION_MISMATCH, (result as DashCueFraming.DecodeResult.Error).errorCode)
    }

    @Test
    fun testGoldenVectorsMatch() {
        // Verify canonical golden test vectors from protocol/fixtures/
        val fixturesDir = File("../../protocol/fixtures")
        if (!fixturesDir.exists()) return

        val frameFiles = fixturesDir.listFiles { _, name -> name.endsWith(".frame") } ?: return
        for (f in frameFiles) {
            val frameBytes = f.readBytes()
            val result = DashCueFraming.decodeFrame(frameBytes)
            assertTrue("Failed to decode golden vector ${f.name}", result is DashCueFraming.DecodeResult.Success)

            val success = result as DashCueFraming.DecodeResult.Success
            val binFile = File(fixturesDir, f.name.replace(".frame", ".bin"))
            if (binFile.exists()) {
                val expectedPayload = binFile.readBytes()
                assertArrayEquals("Payload mismatch for ${f.name}", expectedPayload, success.payload)
            }
        }
    }
}
