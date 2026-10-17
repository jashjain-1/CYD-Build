package com.dashcue.app

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Build
import android.os.IBinder
import com.dashcue.core.ble.DashCueBleService
import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.core.common.DashCueLog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Process-scoped holder for DashCue app state.
 *
 * DashCueBleService is a bound+foreground service; its DashCueBleManager
 * owns the ReconnectionStateMachine. The UI observes the state machine
 * through the manager, obtained once the service is bound.
 */
object AppState {
    private val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    private val _connectionState =
        MutableStateFlow(ReconnectionStateMachine.State.DISCONNECTED)
    val connectionState: StateFlow<ReconnectionStateMachine.State> = _connectionState.asStateFlow()

    var associatedDeviceAddress: String? = null
        private set

    private val _manager = MutableStateFlow<com.dashcue.core.ble.DashCueBleManager?>(null)
    val manager: StateFlow<com.dashcue.core.ble.DashCueBleManager?> = _manager.asStateFlow()

    private var boundService: DashCueBleService? = null
    private var appContext: Context? = null
    private var observingJob: Job? = null
    private var bound = false

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            boundService = (service as? DashCueBleService.LocalBinder)?.getService()
            _manager.value = boundService?.bleManager
            DashCueLog.i("AppState", "DashCueBleService bound")
            observeServiceState()
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            boundService = null
            _manager.value = null
            observingJob?.cancel()
            _connectionState.value = ReconnectionStateMachine.State.DISCONNECTED
            DashCueLog.w("AppState", "DashCueBleService binding cleared")
        }
    }

    fun init(context: Context) {
        if (appContext != null) return
        appContext = context.applicationContext
    }

    private fun observeServiceState() {
        val manager = boundService?.bleManager ?: return
        observingJob?.cancel()
        observingJob = appScope.launch {
            manager.connectionState.collect { state ->
                _connectionState.value = state
            }
        }
    }

    fun bindService(context: Context) {
        if (bound) return
        val ctx = context.applicationContext
        appContext = ctx
        val intent = Intent(ctx, DashCueBleService::class.java)
        bound = ctx.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    fun startConnection(context: Context, deviceAddress: String) {
        val ctx = context.applicationContext
        associatedDeviceAddress = deviceAddress
        val intent = Intent(ctx, DashCueBleService::class.java).apply {
            action = DashCueBleService.ACTION_START_FOREGROUND
            putExtra(DashCueBleService.EXTRA_DEVICE_ADDRESS, deviceAddress)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            ctx.startForegroundService(intent)
        } else {
            ctx.startService(intent)
        }
        bindService(ctx)
    }

    fun stopConnection(context: Context) {
        val ctx = context.applicationContext
        val intent = Intent(ctx, DashCueBleService::class.java).apply {
            action = DashCueBleService.ACTION_STOP_FOREGROUND
        }
        ctx.startService(intent)
        if (bound) {
            ctx.unbindService(serviceConnection)
            bound = false
        }
        observingJob?.cancel()
        observingJob = null
        boundService = null
        _manager.value = null
        _connectionState.value = ReconnectionStateMachine.State.DISCONNECTED
    }
}
