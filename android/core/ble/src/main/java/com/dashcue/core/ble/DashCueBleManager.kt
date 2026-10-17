package com.dashcue.core.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.content.Context
import android.os.BatteryManager
import com.dashcue.core.common.DashCueLog
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.core.protocol.DashCueFragmentation
import com.dashcue.v1.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Measured link telemetry for the Diagnostics tab. */
data class LinkTelemetry(
    val mtu: Int = BleConstants.DEFAULT_ATT_MTU,
    val rssiDbm: Int = 0,
    val packetsIn: Long = 0,
    val packetsOut: Long = 0
)

@SuppressLint("MissingPermission")
class DashCueBleManager(
    private val context: Context,
    private val scope: CoroutineScope,
    val stateMachine: ReconnectionStateMachine = ReconnectionStateMachine()
) {
    companion object {
        private const val TAG = "BleManager"
    }

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter

    private var bluetoothGatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null

    private val fragmentation = DashCueFragmentation()
    private var negotiatedMtu = BleConstants.DEFAULT_ATT_MTU

    private val _connectionState = MutableStateFlow(stateMachine.currentState)
    val connectionState: StateFlow<ReconnectionStateMachine.State> = _connectionState.asStateFlow()

    private val _incomingMessages = MutableStateFlow<DashCueFraming.DecodeResult.Success?>(null)
    val incomingMessages: StateFlow<DashCueFraming.DecodeResult.Success?> = _incomingMessages.asStateFlow()

    /** Unbounded notification stream; unlike the StateFlow above, repeated
     *  identical frames are still delivered (needed for command dispatch). */
    private val _incomingEvents = MutableSharedFlow<DashCueFraming.DecodeResult.Success>(
        extraBufferCapacity = 32
    )
    val incomingEvents = _incomingEvents.asSharedFlow()

    private val _telemetry = MutableStateFlow(LinkTelemetry())
    val telemetry: StateFlow<LinkTelemetry> = _telemetry.asStateFlow()

    private var heartbeatJob: Job? = null
    private var reconnectJob: Job? = null
    private var setupJob: Job? = null
    private var connectionTimeoutJob: Job? = null
    private val sendMutex = Mutex()
    private var pendingWrite: CompletableDeferred<Int>? = null
    private var pendingHelloAck: CompletableDeferred<HelloAck>? = null
    private var peerMaxPayloadSize = DashCueFraming.MAX_PAYLOAD_SIZE
    private var currentTargetMac: String? = null
    var onSnapshotResyncRequired: (suspend (DashCueBleManager) -> Unit)? = null
    private var currentSpeedKmhX10: Int = -1

    fun setReportedSpeed(speedKmhX10: Int) {
        this.currentSpeedKmhX10 = speedKmhX10
    }

    fun startConnecting(deviceAddress: String) {
        disconnect()
        currentTargetMac = deviceAddress
        val bluetoothEnabled = try {
            bluetoothAdapter?.isEnabled == true
        } catch (e: SecurityException) {
            stateMachine.onTerminalError(ReconnectionStateMachine.TerminalReason.PERMISSION_REVOKED)
            _connectionState.value = stateMachine.currentState
            return
        }
        if (!bluetoothEnabled) {
            stateMachine.onTerminalError(ReconnectionStateMachine.TerminalReason.BLUETOOTH_OFF)
            _connectionState.value = stateMachine.currentState
            return
        }

        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            connectInternal(deviceAddress)
        }
    }

    fun disconnect() {
        currentTargetMac = null
        reconnectJob?.cancel()
        releaseGatt()
        stateMachine.reset()
        _connectionState.value = stateMachine.currentState
    }

    private fun releaseGatt() {
        heartbeatJob?.cancel()
        setupJob?.cancel()
        connectionTimeoutJob?.cancel()
        pendingWrite?.complete(BluetoothGatt.GATT_FAILURE)
        pendingWrite = null
        pendingHelloAck?.cancel()
        pendingHelloAck = null
        val oldGatt = bluetoothGatt
        bluetoothGatt = null // Ignore callbacks from the connection being closed.
        try {
            oldGatt?.disconnect()
            oldGatt?.close()
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Error closing GATT", e)
        }
        rxCharacteristic = null
        txCharacteristic = null
        fragmentation.resetAll()
        negotiatedMtu = BleConstants.DEFAULT_ATT_MTU
        peerMaxPayloadSize = DashCueFraming.MAX_PAYLOAD_SIZE
        _telemetry.update { it.copy(mtu = BleConstants.DEFAULT_ATT_MTU) }
    }

    private suspend fun connectInternal(deviceAddress: String) {
        val adapter = bluetoothAdapter ?: return
        val device = try {
            adapter.getRemoteDevice(deviceAddress)
        } catch (e: Exception) {
            DashCueLog.e(TAG, "Invalid Bluetooth MAC address: $deviceAddress", e)
            return
        }

        stateMachine.onConnecting()
        _connectionState.value = stateMachine.currentState

        try {
            bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            if (bluetoothGatt == null) {
                handleDisconnectAndScheduleRetry()
                return
            }
            connectionTimeoutJob = scope.launch {
                delay(120_000L) // Allow the user time to enter the HUD passkey.
                DashCueLog.w(TAG, "Connection setup timed out")
                handleDisconnectAndScheduleRetry()
            }
        } catch (e: SecurityException) {
            stateMachine.onTerminalError(ReconnectionStateMachine.TerminalReason.PERMISSION_REVOKED)
            _connectionState.value = stateMachine.currentState
        }
    }

    // Android invokes these callbacks on Binder threads. Keep all session state on
    // the supplied service scope, and reject late callbacks from previous GATTs.
    private fun withCurrentGatt(gatt: BluetoothGatt, block: () -> Unit) {
        scope.launch {
            if (gatt !== bluetoothGatt) return@launch
            try {
                block()
            } catch (e: SecurityException) {
                releaseGatt()
                stateMachine.onTerminalError(ReconnectionStateMachine.TerminalReason.PERMISSION_REVOKED)
                _connectionState.value = stateMachine.currentState
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            withCurrentGatt(gatt) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                DashCueLog.w(TAG, "GATT connection status error: $status, newState: $newState")
                handleDisconnectAndScheduleRetry()
                return@withCurrentGatt
            }

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                DashCueLog.i(TAG, "GATT connected, requesting MTU ${BleConstants.TARGET_ATT_MTU}")
                stateMachine.onNegotiating()
                _connectionState.value = stateMachine.currentState
                if (!gatt.requestMtu(BleConstants.TARGET_ATT_MTU)) discoverServices(gatt)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                DashCueLog.i(TAG, "GATT disconnected")
                handleDisconnectAndScheduleRetry()
            }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            withCurrentGatt(gatt) {
            negotiatedMtu = if (status == BluetoothGatt.GATT_SUCCESS)
                mtu.coerceIn(BleConstants.DEFAULT_ATT_MTU, BleConstants.TARGET_ATT_MTU)
            else BleConstants.DEFAULT_ATT_MTU
            _telemetry.update { it.copy(mtu = negotiatedMtu) }
            DashCueLog.i(TAG, "MTU negotiated: $negotiatedMtu, discovering services...")
            discoverServices(gatt)
            }
        }

        override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, status: Int) {
            withCurrentGatt(gatt) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    _telemetry.update { it.copy(rssiDbm = rssi) }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            withCurrentGatt(gatt) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                handleDisconnectAndScheduleRetry()
                return@withCurrentGatt
            }

            val service = gatt.getService(BleConstants.SERVICE_UUID)
            if (service == null) {
                DashCueLog.e(TAG, "DashCue HUD GATT Service not found on device!")
                handleDisconnectAndScheduleRetry()
                return@withCurrentGatt
            }

            rxCharacteristic = service.getCharacteristic(BleConstants.RX_CHAR_UUID)
            txCharacteristic = service.getCharacteristic(BleConstants.TX_CHAR_UUID)

            val tx = txCharacteristic
            val descriptor = tx?.getDescriptor(BleConstants.CCCD_UUID)
            if (rxCharacteristic == null || tx == null || descriptor == null ||
                !gatt.setCharacteristicNotification(tx, true)) {
                DashCueLog.w(TAG, "Required GATT characteristics or notification descriptor missing")
                handleDisconnectAndScheduleRetry()
                return@withCurrentGatt
            }
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            if (!gatt.writeDescriptor(descriptor)) handleDisconnectAndScheduleRetry()
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            withCurrentGatt(gatt) {
                if (descriptor.uuid != BleConstants.CCCD_UUID) return@withCurrentGatt
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    handleDisconnectAndScheduleRetry()
                    return@withCurrentGatt
                }
                synchronise(gatt)
            }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            withCurrentGatt(gatt) {
                if (characteristic.uuid == BleConstants.RX_CHAR_UUID) pendingWrite?.complete(status)
            }
        }

        @Deprecated("Android 8–12 callback")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            receiveNotification(gatt, characteristic.uuid, characteristic.value?.copyOf() ?: return)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            receiveNotification(gatt, characteristic.uuid, value.copyOf())
        }
    }

    private fun discoverServices(gatt: BluetoothGatt) {
        stateMachine.onServicesDiscovered()
        _connectionState.value = stateMachine.currentState
        if (!gatt.discoverServices()) handleDisconnectAndScheduleRetry()
    }

    private fun synchronise(gatt: BluetoothGatt) {
        stateMachine.onSynchronising()
        _connectionState.value = stateMachine.currentState
        setupJob?.cancel()
        setupJob = scope.launch {
            val ackWaiter = CompletableDeferred<HelloAck>()
            pendingHelloAck = ackWaiter
            if (!sendHello()) {
                handleDisconnectAndScheduleRetry()
                return@launch
            }
            val ack = withTimeoutOrNull(10_000L) { ackWaiter.await() }
            pendingHelloAck = null
            if (gatt !== bluetoothGatt) return@launch
            if (ack == null) {
                DashCueLog.w(TAG, "HUD did not acknowledge HELLO")
                handleDisconnectAndScheduleRetry()
                return@launch
            }
            if (ack.accepted != 1 || ack.protoMajor != 1 || ack.maxMsgSize <= 0) {
                releaseGatt()
                stateMachine.onTerminalError(ReconnectionStateMachine.TerminalReason.VERSION_MISMATCH)
                _connectionState.value = stateMachine.currentState
                return@launch
            }
            peerMaxPayloadSize = minOf(ack.maxMsgSize, DashCueFraming.MAX_PAYLOAD_SIZE)
            onSnapshotResyncRequired?.invoke(this@DashCueBleManager)
            if (gatt !== bluetoothGatt) return@launch
            connectionTimeoutJob?.cancel()
            stateMachine.onReady()
            _connectionState.value = stateMachine.currentState
            try {
                gatt.requestConnectionPriority(android.bluetooth.BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                DashCueLog.i(TAG, "Requested BLE CONNECTION_PRIORITY_HIGH for fast, reliable bulk lane throughput")
            } catch (e: Exception) {
                DashCueLog.w(TAG, "Could not request CONNECTION_PRIORITY_HIGH", e)
            }
            startHeartbeatLoop()
        }
    }

    private fun receiveNotification(gatt: BluetoothGatt, uuid: java.util.UUID, value: ByteArray) {
        withCurrentGatt(gatt) {
            if (uuid == BleConstants.TX_CHAR_UUID) {
                val result = fragmentation.onFragmentReceived(value)
                if (result is DashCueFragmentation.ReassemblyResult.Complete) {
                    val decoded = DashCueFraming.decodeFrame(result.frame)
                    if (decoded is DashCueFraming.DecodeResult.Success) {
                        stateMachine.onPacketReceived()
                        _telemetry.update { it.copy(packetsIn = it.packetsIn + 1) }
                        _incomingMessages.value = decoded
                        _incomingEvents.tryEmit(decoded)
                        if (decoded.header.msgType == MessageType.MT_HELLO_ACK_VALUE &&
                            decoded.header.msgId == 1001L) {
                            try {
                                pendingHelloAck?.complete(HelloAck.parseFrom(decoded.payload))
                            } catch (e: com.google.protobuf.InvalidProtocolBufferException) {
                                DashCueLog.w(TAG, "Malformed HELLO_ACK", e)
                            }
                        }
                        if (decoded.header.msgType == MessageType.MT_SNAPSHOT_REQUEST_VALUE) {
                            scope.launch {
                                onSnapshotResyncRequired?.invoke(this@DashCueBleManager)
                            }
                        }
                    } else if (decoded is DashCueFraming.DecodeResult.Error) {
                        DashCueLog.w(TAG, "Frame decode error: ${decoded.description}")
                    }
                }
            }
        }
    }

    private fun handleDisconnectAndScheduleRetry() {
        releaseGatt()
        if (currentTargetMac == null) return
        if (stateMachine.currentState != ReconnectionStateMachine.State.DISCONNECTED) stateMachine.onDisconnected()
        _connectionState.value = stateMachine.currentState

        if (stateMachine.currentState == ReconnectionStateMachine.State.TERMINAL_ERROR) {
            return
        }

        val delayMs = stateMachine.getNextDelayMs()
        DashCueLog.i(TAG, "Scheduling reconnection in $delayMs ms (backoff step ${stateMachine.backoffIndex})")
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            delay(delayMs)
            val mac = currentTargetMac
            if (mac != null) {
                connectInternal(mac)
            }
        }
    }

    private fun startHeartbeatLoop() {
        heartbeatJob?.cancel()
        heartbeatJob = scope.launch {
            var seq = 1
            while (isActive && stateMachine.currentState == ReconnectionStateMachine.State.READY) {
                delay(BleConstants.HEARTBEAT_INTERVAL_MS)

                // Check link loss (no packet for >= 6.0s)
                if (stateMachine.checkLinkLoss()) {
                    DashCueLog.w(TAG, "Link dead detected (no packet in 6s), disconnecting...")
                    handleDisconnectAndScheduleRetry()
                    break
                }

                // Send heartbeat
                val heartbeat = Heartbeat.newBuilder()
                    .setPhoneTsMs(System.currentTimeMillis())
                    .setTzOffsetMin(java.util.TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 60000)
                    .setCredits(8)
                    .setSpeedKmhX10(currentSpeedKmhX10)
                    .setSeq(seq)
                    .setRttMs(0) // Unknown until measured; do not report a fabricated RTT.
                    .setPhoneBatteryPct(readBatteryPercent())
                    .build()

                if (!sendMessage(MessageType.MT_HEARTBEAT.number, seq.toLong(), 0, heartbeat.toByteArray())) {
                    handleDisconnectAndScheduleRetry()
                    break
                }
                // RSSI is not pushed by the stack; poll it with the heartbeat.
                try {
                    bluetoothGatt?.readRemoteRssi()
                } catch (e: SecurityException) {
                    DashCueLog.w(TAG, "RSSI read denied", e)
                }
                seq++
            }
        }
    }

    /**
     * Battery percentage for the HUD status bar. Returns 0 when the platform
     * cannot answer (emulator, broken gauge); the HUD renders that as unknown
     * rather than a fabricated 100 %.
     */
    private fun readBatteryPercent(): Int {
        val manager = context.getSystemService(Context.BATTERY_SERVICE) as? BatteryManager
            ?: return 0
        return try {
            manager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY).coerceIn(0, 100)
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Battery capacity unavailable", e)
            0
        }
    }

    suspend fun sendMessage(
        msgType: Int,
        msgId: Long,
        flags: Int = 0,
        payload: ByteArray,
        lane: Int = DashCueFragmentation.LANE_CONTROL
    ): Boolean = sendMutex.withLock {
        if (stateMachine.currentState != ReconnectionStateMachine.State.READY &&
            stateMachine.currentState != ReconnectionStateMachine.State.SYNCHRONISING
        ) {
            return@withLock false
        }
        // At MTU 23 the 8-bit fragment count limits a full frame to 4080 bytes.
        val transportPayloadLimit = minOf(peerMaxPayloadSize, 255 * (negotiatedMtu - 7) - DashCueFraming.MIN_FRAME_SIZE)
        if (payload.size > transportPayloadLimit || lane !in 0 until DashCueFragmentation.NUM_LANES) return@withLock false
        val gatt = bluetoothGatt ?: return@withLock false
        val char = rxCharacteristic ?: return@withLock false
        val frame = DashCueFraming.encodeFrame(msgType, msgId, flags, payload)
        val fragments = fragmentation.fragment(frame, lane, negotiatedMtu)
        _telemetry.update { it.copy(packetsOut = it.packetsOut + 1) }

        for (frag in fragments) {
            if (gatt !== bluetoothGatt) return@withLock false
            val completion = CompletableDeferred<Int>()
            pendingWrite = completion
            try {
                char.value = frag
                // A single acknowledged fragment at a time bounds pressure on the
                // ESP32 RX queue and prevents interleaved same-lane frames.
                char.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                if (!gatt.writeCharacteristic(char)) {
                    handleDisconnectAndScheduleRetry()
                    return@withLock false
                }
                if (withTimeoutOrNull(60_000L) { completion.await() } != BluetoothGatt.GATT_SUCCESS) {
                    handleDisconnectAndScheduleRetry()
                    return@withLock false
                }
            } catch (e: CancellationException) {
                // Discard partial reassembly and any late write completion before
                // another sender can use this connection.
                if (gatt === bluetoothGatt) handleDisconnectAndScheduleRetry()
                throw e
            } catch (e: SecurityException) {
                releaseGatt()
                stateMachine.onTerminalError(ReconnectionStateMachine.TerminalReason.PERMISSION_REVOKED)
                _connectionState.value = stateMachine.currentState
                return@withLock false
            } finally {
                if (pendingWrite === completion) pendingWrite = null
            }
        }
        true
    }

    private suspend fun sendHello(): Boolean {
        val hello = Hello.newBuilder()
            .setProtoMajor(1)
            .setProtoMinor(0)
            .setCapabilities(0) // Feature pipelines are not wired in this build.
            .setDeviceId("AndroidPhone")
            .setFwVersion("1.0.0")
            .setHwRevision("Android")
            .setArtworkSize(96)
            .setMaxMsgSize(DashCueFraming.MAX_PAYLOAD_SIZE)
            .setMtu(negotiatedMtu)
            .setWindowMax(1)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()

        return sendMessage(MessageType.MT_HELLO.number, 1001L, 0, hello.toByteArray())
    }
}
