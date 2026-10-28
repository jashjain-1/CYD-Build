package com.dashcue.core.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32

/**
 * Binary wire framing layer matching DashCue protocol version 1.0 (spec §9.3).
 *
 * 16-byte Header (Little-Endian):
 *   - uint16 magic (0xD45C)
 *   - uint8  version (0x01)
 *   - uint8  flags (EnvelopeFlag bitmask)
 *   - uint16 msg_type (MessageType enum)
 *   - uint32 msg_id (Sequence / correlation / command ID)
 *   - uint32 length (Protobuf payload length, <= 2048)
 *   - uint16 reserved (0)
 * Payload:
 *   - length bytes (raw serialized protobuf)
 * Trailer:
 *   - uint32 crc32 (IEEE 802.3 CRC32 over header + payload)
 */
object DashCueFraming {
    const val FRAME_MAGIC: Int = 0xD45C
    const val FRAME_VERSION: Int = 0x01
    const val HEADER_SIZE: Int = 16
    const val TRAILER_SIZE: Int = 4
    const val MIN_FRAME_SIZE: Int = HEADER_SIZE + TRAILER_SIZE // 20
    const val MAX_PAYLOAD_SIZE: Int = 2048
    const val MAX_FRAME_SIZE: Int = HEADER_SIZE + MAX_PAYLOAD_SIZE + TRAILER_SIZE // 2068

    data class Header(
        val magic: Int = FRAME_MAGIC,
        val version: Int = FRAME_VERSION,
        val flags: Int = 0,
        val msgType: Int,
        val msgId: Long,
        val length: Int,
        val reserved: Int = 0
    )

    sealed class DecodeResult {
        data class Success(val header: Header, val payload: ByteArray) : DecodeResult()
        data class Error(val errorCode: Int, val description: String) : DecodeResult()
    }

    // Protocol ErrorCode constants matching dashcue.proto
    const val EC_NONE = 0
    const val EC_TOO_LARGE = 1
    const val EC_CRC = 2
    const val EC_VERSION_MISMATCH = 3
    const val EC_UNKNOWN_TYPE = 5
    const val EC_DECODE = 6

    fun calculateCrc32(data: ByteArray, offset: Int = 0, length: Int = data.size): Long {
        val crc = CRC32()
        crc.update(data, offset, length)
        return crc.value and 0xFFFFFFFFL
    }

    /**
     * Encodes header metadata and payload into an enveloped frame with 32-bit CRC.
     */
    fun encodeFrame(
        msgType: Int,
        msgId: Long,
        flags: Int = 0,
        payload: ByteArray
    ): ByteArray {
        require(payload.size <= MAX_PAYLOAD_SIZE) {
            "Payload size ${payload.size} exceeds MAX_PAYLOAD_SIZE ($MAX_PAYLOAD_SIZE)"
        }

        val totalSize = HEADER_SIZE + payload.size + TRAILER_SIZE
        val buffer = ByteBuffer.allocate(totalSize).order(ByteOrder.LITTLE_ENDIAN)

        buffer.putShort(FRAME_MAGIC.toShort())
        buffer.put(FRAME_VERSION.toByte())
        buffer.put(flags.toByte())
        buffer.putShort(msgType.toShort())
        buffer.putInt((msgId and 0xFFFFFFFFL).toInt())
        buffer.putInt(payload.size)
        buffer.putShort(0.toShort()) // reserved

        buffer.put(payload)

        // CRC32 over header + payload
        val crc = calculateCrc32(buffer.array(), 0, HEADER_SIZE + payload.size)
        buffer.putInt((crc and 0xFFFFFFFFL).toInt())

        return buffer.array()
    }

    /**
     * Decodes and validates an incoming frame buffer.
     */
    fun decodeFrame(frameBytes: ByteArray): DecodeResult {
        if (frameBytes.size < MIN_FRAME_SIZE) {
            return DecodeResult.Error(EC_DECODE, "Frame smaller than minimum frame size ($MIN_FRAME_SIZE)")
        }
        if (frameBytes.size > MAX_FRAME_SIZE) {
            return DecodeResult.Error(EC_TOO_LARGE, "Frame exceeds maximum size ($MAX_FRAME_SIZE)")
        }

        val buffer = ByteBuffer.wrap(frameBytes).order(ByteOrder.LITTLE_ENDIAN)

        val magic = buffer.short.toInt() and 0xFFFF
        if (magic != FRAME_MAGIC) {
            return DecodeResult.Error(EC_DECODE, "Invalid frame magic: 0x${magic.toString(16).uppercase()}")
        }

        val version = buffer.get().toInt() and 0xFF
        if (version != FRAME_VERSION) {
            return DecodeResult.Error(EC_VERSION_MISMATCH, "Unsupported frame version: $version")
        }

        val flags = buffer.get().toInt() and 0xFF
        val msgType = buffer.short.toInt() and 0xFFFF
        val msgId = buffer.int.toLong() and 0xFFFFFFFFL
        val payloadLen = buffer.int
        val reserved = buffer.short.toInt() and 0xFFFF

        if (payloadLen < 0 || payloadLen > MAX_PAYLOAD_SIZE) {
            return DecodeResult.Error(EC_TOO_LARGE, "Payload length out of bounds: $payloadLen")
        }

        val expectedTotalSize = HEADER_SIZE + payloadLen + TRAILER_SIZE
        if (frameBytes.size != expectedTotalSize) {
            return DecodeResult.Error(
                EC_DECODE,
                "Frame length mismatch: expected $expectedTotalSize, received ${frameBytes.size}"
            )
        }

        val calculatedCrc = calculateCrc32(frameBytes, 0, HEADER_SIZE + payloadLen)
        buffer.position(HEADER_SIZE + payloadLen)
        val expectedCrc = buffer.int.toLong() and 0xFFFFFFFFL

        if (calculatedCrc != expectedCrc) {
            return DecodeResult.Error(
                EC_CRC,
                "CRC32 mismatch: calculated 0x${calculatedCrc.toString(16).uppercase()}, expected 0x${expectedCrc.toString(16).uppercase()}"
            )
        }

        val payload = ByteArray(payloadLen)
        System.arraycopy(frameBytes, HEADER_SIZE, payload, 0, payloadLen)

        val header = Header(
            magic = magic,
            version = version,
            flags = flags,
            msgType = msgType,
            msgId = msgId,
            length = payloadLen,
            reserved = reserved
        )

        return DecodeResult.Success(header, payload)
    }
}
