package com.dashcue.core.ble

import android.bluetooth.*
import android.content.Context
import com.dashcue.core.protocol.DashCueFragmentation
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.v1.HelloAck
import com.dashcue.v1.MessageType
import io.mockk.*
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Callback contract tests only. These do not substitute for phone/HUD testing. */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class DashCueBleManagerTest {
    private class Rig(scope: CoroutineScope) {
        val context = mockk<Context>()
        val adapter = mockk<BluetoothAdapter>()
        val device = mockk<BluetoothDevice>()
        val gatt = mockk<BluetoothGatt>(relaxed = true)
        val service = mockk<BluetoothGattService>()
        val rx = mockk<BluetoothGattCharacteristic>(relaxed = true)
        val tx = mockk<BluetoothGattCharacteristic>(relaxed = true)
        val cccd = mockk<BluetoothGattDescriptor>(relaxed = true)
        val callback = slot<BluetoothGattCallback>()
        val writes = mutableListOf<ByteArray>()
        private var outgoing = byteArrayOf()
        val manager: DashCueBleManager

        init {
            val btManager = mockk<BluetoothManager>()
            every { context.getSystemService(Context.BLUETOOTH_SERVICE) } returns btManager
            every { btManager.adapter } returns adapter
            every { adapter.isEnabled } returns true
            every { adapter.getRemoteDevice(any<String>()) } returns device
            every { device.connectGatt(context, false, capture(callback), BluetoothDevice.TRANSPORT_LE) } returns gatt
            every { gatt.requestMtu(any()) } returns true
            every { gatt.discoverServices() } returns true
            every { gatt.getService(BleConstants.SERVICE_UUID) } returns service
            every { service.getCharacteristic(BleConstants.RX_CHAR_UUID) } returns rx
            every { service.getCharacteristic(BleConstants.TX_CHAR_UUID) } returns tx
            every { rx.uuid } returns BleConstants.RX_CHAR_UUID
            every { tx.uuid } returns BleConstants.TX_CHAR_UUID
            every { tx.getDescriptor(BleConstants.CCCD_UUID) } returns cccd
            every { cccd.uuid } returns BleConstants.CCCD_UUID
            every { gatt.setCharacteristicNotification(tx, true) } returns true
            every { gatt.writeDescriptor(cccd) } returns true
            every { rx.setValue(any<ByteArray>()) } answers { outgoing = firstArg<ByteArray>().copyOf(); true }
            every { gatt.writeCharacteristic(rx) } answers { writes.add(outgoing.copyOf()); true }
            manager = DashCueBleManager(context, scope)
        }

        fun acknowledgeWrite() = callback.captured.onCharacteristicWrite(gatt, rx, BluetoothGatt.GATT_SUCCESS)

        fun helloAck(accepted: Int = 1, legacy: Boolean = false) {
            val payload = HelloAck.newBuilder().setProtoMajor(1).setAccepted(accepted).setMaxMsgSize(4096).build().toByteArray()
            val frame = DashCueFraming.encodeFrame(MessageType.MT_HELLO_ACK_VALUE, 1001L, 0, payload)
            for (packet in DashCueFragmentation().fragment(frame)) {
                if (legacy) {
                    every { tx.value } returns packet
                    callback.captured.onCharacteristicChanged(gatt, tx)
                } else callback.captured.onCharacteristicChanged(gatt, tx, packet)
            }
        }
    }

    private fun TestScope.discover(rig: Rig, mtu: Int = 247) {
        rig.manager.startConnecting("AA:BB:CC:DD:EE:FF")
        runCurrent()
        rig.callback.captured.onConnectionStateChange(rig.gatt, BluetoothGatt.GATT_SUCCESS, BluetoothProfile.STATE_CONNECTED)
        runCurrent()
        rig.callback.captured.onMtuChanged(rig.gatt, mtu, BluetoothGatt.GATT_SUCCESS)
        runCurrent()
        rig.callback.captured.onServicesDiscovered(rig.gatt, BluetoothGatt.GATT_SUCCESS)
        runCurrent()
    }

    private fun TestScope.handshake(rig: Rig) {
        discover(rig)
        rig.callback.captured.onDescriptorWrite(rig.gatt, rig.cccd, BluetoothGatt.GATT_SUCCESS)
        runCurrent()
        rig.acknowledgeWrite()
        runCurrent()
        rig.helloAck()
        runCurrent()
        assertEquals(ReconnectionStateMachine.State.READY, rig.manager.connectionState.value)
    }

    @Test fun waitsForNotificationSubscriptionAndHelloAck() = runTest {
        val rig = Rig(backgroundScope)
        discover(rig)
        assertTrue("No HELLO may race the CCCD write", rig.writes.isEmpty())
        rig.callback.captured.onDescriptorWrite(rig.gatt, rig.cccd, BluetoothGatt.GATT_SUCCESS)
        runCurrent()
        assertEquals(1, rig.writes.size)
        rig.acknowledgeWrite()
        runCurrent()
        assertEquals(ReconnectionStateMachine.State.SYNCHRONISING, rig.manager.connectionState.value)
        rig.helloAck()
        runCurrent()
        assertEquals(ReconnectionStateMachine.State.READY, rig.manager.connectionState.value)
    }

    @Test fun failedCccdNeverSendsHelloOrReportsReady() = runTest {
        val rig = Rig(backgroundScope)
        discover(rig)
        rig.callback.captured.onDescriptorWrite(rig.gatt, rig.cccd, BluetoothGatt.GATT_FAILURE)
        runCurrent()
        assertTrue(rig.writes.isEmpty())
        assertEquals(ReconnectionStateMachine.State.DISCONNECTED, rig.manager.connectionState.value)
        verify(exactly = 1) { rig.gatt.close() }
    }

    @Test fun supportsPreAndroid13NotificationCallback() = runTest {
        val rig = Rig(backgroundScope)
        discover(rig)
        rig.callback.captured.onDescriptorWrite(rig.gatt, rig.cccd, BluetoothGatt.GATT_SUCCESS)
        runCurrent()
        rig.acknowledgeWrite()
        runCurrent()
        rig.helloAck(legacy = true)
        runCurrent()
        assertEquals(ReconnectionStateMachine.State.READY, rig.manager.connectionState.value)
    }

    @Test fun rejectedHelloClosesConnection() = runTest {
        val rig = Rig(backgroundScope)
        discover(rig)
        rig.callback.captured.onDescriptorWrite(rig.gatt, rig.cccd, BluetoothGatt.GATT_SUCCESS)
        runCurrent()
        rig.acknowledgeWrite()
        runCurrent()
        rig.helloAck(accepted = 0)
        runCurrent()
        assertEquals(ReconnectionStateMachine.State.TERMINAL_ERROR, rig.manager.connectionState.value)
        verify(exactly = 1) { rig.gatt.close() }
    }

    @Test fun concurrentMessagesWaitForEachWriteAndNeverInterleaveFrames() = runTest {
        val rig = Rig(backgroundScope)
        handshake(rig)
        rig.writes.clear()
        val first = async { rig.manager.sendMessage(50, 2001, payload = ByteArray(300)) }
        val second = async { rig.manager.sendMessage(50, 2002, payload = ByteArray(2)) }
        runCurrent()
        assertEquals(1, rig.writes.size)
        assertEquals(0, rig.writes[0][2].toInt())
        rig.acknowledgeWrite()
        runCurrent()
        assertEquals(2, rig.writes.size)
        assertEquals(1, rig.writes[1][2].toInt())
        rig.acknowledgeWrite()
        runCurrent()
        assertTrue(first.await())
        assertEquals(3, rig.writes.size)
        assertEquals(0, rig.writes[2][2].toInt())
        rig.acknowledgeWrite()
        runCurrent()
        assertTrue(second.await())
    }

    @Test fun explicitDisconnectIgnoresLateCallbackAndDoesNotReconnect() = runTest {
        val rig = Rig(backgroundScope)
        discover(rig)
        rig.manager.disconnect()
        rig.callback.captured.onConnectionStateChange(rig.gatt, BluetoothGatt.GATT_SUCCESS, BluetoothProfile.STATE_DISCONNECTED)
        runCurrent()
        advanceTimeBy(120_001L)
        runCurrent()
        assertEquals(ReconnectionStateMachine.State.DISCONNECTED, rig.manager.connectionState.value)
        verify(exactly = 1) { rig.device.connectGatt(any(), any(), any(), any()) }
    }

    @Test fun rejectsUnrepresentablePayloadAtMtu23WithoutWriting() = runTest {
        val rig = Rig(backgroundScope)
        discover(rig, mtu = 23)
        rig.manager.stateMachine.onReady()
        assertFalse(rig.manager.sendMessage(50, 2003, payload = ByteArray(4061)))
        assertTrue(rig.writes.isEmpty())
    }
}
