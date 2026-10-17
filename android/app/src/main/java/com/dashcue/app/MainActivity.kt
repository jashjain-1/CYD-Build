package com.dashcue.app

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.MonitorHeart
import androidx.compose.material.icons.filled.Navigation
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.app.ActivityCompat
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.dashcue.app.ui.CockpitTopBar
import com.dashcue.app.ui.DashCueColors
import com.dashcue.app.ui.DashCueTheme
import com.dashcue.app.ui.DashboardScreen
import com.dashcue.app.ui.DiagnosticsScreen
import com.dashcue.app.ui.NavigationScreen
import com.dashcue.app.ui.SettingsScreen
import com.dashcue.core.ble.CompanionDeviceHelper
import com.dashcue.core.ble.DashCueBleService
import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.core.common.DashCueLog
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch

enum class PermissionStep(val label: String, val rationale: String) {
    BLUETOOTH("Nearby devices", "Needed to find and connect to the DashCue HUD over Bluetooth LE."),
    LOCATION("Precise location", "Navigation needs your position to guide you turn by turn."),
    NOTIFICATIONS("Notifications", "Shows the permanent status card while connected or navigating.")
}

enum class PermissionStatus { NOT_REQUESTED, GRANTED, DENIED }

enum class AppStage { PERMISSIONS, READY }

class MainActivity : ComponentActivity() {

    private val cdmHelper by lazy { CompanionDeviceHelper(this) }
    private val stateMachine = ReconnectionStateMachine()
    private var pendingPermissionStep: PermissionStep? = null
    private var stage by mutableStateOf(AppStage.PERMISSIONS)
    private val permissionStatuses = mutableStateMapOf(
        PermissionStep.BLUETOOTH.name to PermissionStatus.NOT_REQUESTED,
        PermissionStep.LOCATION.name to PermissionStatus.NOT_REQUESTED,
        PermissionStep.NOTIFICATIONS.name to PermissionStatus.NOT_REQUESTED
    )
    private val connectionText = mutableStateOf("Disconnected — tap Pair to connect")

    private val permissionLauncher: ActivityResultLauncher<Array<String>> =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
            val granted = result.values.all { it }
            refreshPermissionStatuses()
            if (!granted) {
                pendingPermissionStep?.let {
                    permissionStatuses[it.name] = PermissionStatus.DENIED
                }
            }
            if (allPermissionsResolved() && stage == AppStage.PERMISSIONS) {
                stage = AppStage.READY
            }
        }

    private val cdmPairLauncher =
        registerForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { result ->
            if (result.resultCode == RESULT_OK) {
                DashCueLog.i("MainActivity", "CDM association result OK")
                val address = cdmHelper.lookupAssociatedDeviceAddress()
                if (!address.isNullOrBlank()) {
                    AppState.startConnection(this, address)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        cdmHelper.startObservingPresence(address)
                    }
                } else {
                    DashCueLog.w("MainActivity", "CDM OK but no MAC recovered")
                    connectionText.value = "Pairing completed, but no HUD address was returned. Try Pair again."
                }
            } else {
                DashCueLog.w("MainActivity", "CDM association closed with ${result.resultCode}")
                connectionText.value = "Pairing cancelled — tap Pair to try again"
            }
        }

    private fun allPermissionsResolved(): Boolean =
        permissionStatuses.values.all { it == PermissionStatus.GRANTED }

    private fun refreshPermissionStatuses() {
        permissionStatuses[PermissionStep.BLUETOOTH.name] = when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
                if (has(Manifest.permission.BLUETOOTH_CONNECT) && has(Manifest.permission.BLUETOOTH_SCAN))
                    PermissionStatus.GRANTED else PermissionStatus.NOT_REQUESTED
            else -> if (has(Manifest.permission.ACCESS_FINE_LOCATION)) PermissionStatus.GRANTED else PermissionStatus.NOT_REQUESTED
        }
        permissionStatuses[PermissionStep.LOCATION.name] =
            if (has(Manifest.permission.ACCESS_FINE_LOCATION)) PermissionStatus.GRANTED else PermissionStatus.NOT_REQUESTED
        permissionStatuses[PermissionStep.NOTIFICATIONS.name] =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                if (has(Manifest.permission.POST_NOTIFICATIONS)) PermissionStatus.GRANTED else PermissionStatus.NOT_REQUESTED
            } else PermissionStatus.GRANTED
    }

    private fun has(permission: String): Boolean =
        ActivityCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppState.init(this)
        refreshPermissionStatuses()
        if (allPermissionsResolved()) stage = AppStage.READY

        AppState.connectionState
            .onEach { st -> connectionText.value = describeState(st) }
            .launchIn(lifecycleScope)

        setContent { DashCueRootUi() }
    }

    private fun describeState(state: ReconnectionStateMachine.State): String = when (state) {
        ReconnectionStateMachine.State.DISCONNECTED -> "Disconnected — tap Pair to connect"
        ReconnectionStateMachine.State.SCANNING -> "Scanning for HUD…"
        ReconnectionStateMachine.State.CONNECTING -> "Connecting…"
        ReconnectionStateMachine.State.DISCOVERING_SERVICES -> "Discovering services…"
        ReconnectionStateMachine.State.NEGOTIATING -> "Negotiating link…"
        ReconnectionStateMachine.State.SYNCHRONISING -> "Synchronising…"
        ReconnectionStateMachine.State.READY -> "Connected"
        ReconnectionStateMachine.State.TERMINAL_ERROR -> "Error — check Bluetooth and permissions"
    }

    private fun requestCdmPairing() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !has(Manifest.permission.BLUETOOTH_CONNECT)) {
            refreshPermissionStatuses()
            stage = AppStage.PERMISSIONS
            return
        }
        val adapter = getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter?.state != BluetoothAdapter.STATE_ON) {
            DashCueLog.w("MainActivity", "Bluetooth is off; cannot pair")
            connectionText.value = "Bluetooth is off — turn it on and try again"
            return
        }
        cdmHelper.associate(
            object : CompanionDeviceHelper.Callback {
                override fun onAssociationPending(chooserLauncher: android.content.IntentSender) {
                    cdmPairLauncher.launch(IntentSenderRequest.Builder(chooserLauncher).build())
                }

                override fun onFailure(error: String) {
                    DashCueLog.w("MainActivity", "CDM pairing failed: $error")
                    connectionText.value = "Pairing failed: $error"
                }
            }
        )
    }

    @Composable
    private fun DashCueRootUi() {
        DashCueTheme {
            when (stage) {
                AppStage.PERMISSIONS -> PermissionsScreen()
                AppStage.READY -> MainShell()
            }
        }
    }

    @Composable
    private fun MainShell() {
        val dashboardViewModel: DashCueViewModel = viewModel()
        var selectedTab by rememberSaveable { mutableIntStateOf(0) }
        val snackbar = remember { SnackbarHostState() }
        val connection by dashboardViewModel.connectionState.collectAsStateWithLifecycle()

        LaunchedEffect(dashboardViewModel) {
            dashboardViewModel.messages.collect { snackbar.showSnackbar(it) }
        }

        Scaffold(
            containerColor = DashCueColors.Bg,
            topBar = { CockpitTopBar(connection) },
            snackbarHost = { SnackbarHost(snackbar) },
            bottomBar = {
                NavigationBar(containerColor = DashCueColors.Surface) {
                    val items = listOf(
                        Triple("Dashboard", Icons.Filled.Dashboard, 0),
                        Triple("Navigation", Icons.Filled.Navigation, 1),
                        Triple("Settings", Icons.Filled.Settings, 2),
                        Triple("Diagnostics", Icons.Filled.MonitorHeart, 3)
                    )
                    items.forEach { (label, icon, index) ->
                        NavigationBarItem(
                            selected = selectedTab == index,
                            onClick = { selectedTab = index },
                            icon = { Icon(icon, contentDescription = label) },
                            label = { Text(label, fontSize = 11.sp) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = DashCueColors.Bg,
                                selectedTextColor = DashCueColors.Accent,
                                indicatorColor = DashCueColors.Accent,
                                unselectedIconColor = DashCueColors.TextDim,
                                unselectedTextColor = DashCueColors.TextDim
                            )
                        )
                    }
                }
            }
        ) { innerPadding ->
            Box(
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
            ) {
                when (selectedTab) {
                    0 -> DashboardScreen(dashboardViewModel, onPairRequested = ::requestCdmPairing)
                    1 -> NavigationScreen(dashboardViewModel)
                    2 -> SettingsScreen(dashboardViewModel, onPairRequested = ::requestCdmPairing)
                    else -> DiagnosticsScreen(dashboardViewModel)
                }
            }
        }
    }

    @Composable
    private fun PermissionRow(step: PermissionStep) {
        val status = permissionStatuses[step.name] ?: PermissionStatus.NOT_REQUESTED
        Card(colors = CardDefaults.cardColors(containerColor = DashCueColors.Surface)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                androidx.compose.foundation.layout.Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        step.label,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = DashCueColors.Text,
                        modifier = Modifier.weight(1f)
                    )
                    when (status) {
                        PermissionStatus.GRANTED -> Text("Granted", color = DashCueColors.Ok, fontSize = 13.sp)
                        PermissionStatus.DENIED -> Text("Denied", color = DashCueColors.Warn, fontSize = 13.sp)
                        PermissionStatus.NOT_REQUESTED -> {}
                    }
                }
                Text(step.rationale, style = MaterialTheme.typography.bodySmall, color = DashCueColors.TextDim)
                if (status != PermissionStatus.GRANTED) {
                    Button(
                        onClick = { launchPermissionFlow(step) },
                        colors = ButtonDefaults.buttonColors(containerColor = DashCueColors.Accent)
                    ) { Text("Grant", color = DashCueColors.Bg) }
                }
            }
        }
    }

    @Composable
    private fun PermissionsScreen() {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                "DashCue setup",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                color = DashCueColors.Text
            )
            Text(
                "DashCue pairs your phone with the DashCue HUD mounted on your dash. Grant the " +
                    "permissions below so the app can find the HUD, navigate and stay connected in the background.",
                style = MaterialTheme.typography.bodyMedium,
                color = DashCueColors.TextDim
            )
            PermissionStep.entries.forEach { step -> PermissionRow(step) }
            if (allPermissionsResolved()) {
                Button(
                    onClick = { stage = AppStage.READY },
                    colors = ButtonDefaults.buttonColors(containerColor = DashCueColors.Accent),
                    modifier = Modifier.fillMaxWidth()
                ) { Text("Continue", color = DashCueColors.Bg) }
            } else {
                Text(
                    "Grant all permissions to continue.",
                    style = MaterialTheme.typography.bodySmall,
                    color = DashCueColors.Warn
                )
            }
        }
    }

    private fun launchPermissionFlow(step: PermissionStep) {
        pendingPermissionStep = step
        when (step) {
            PermissionStep.BLUETOOTH -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    permissionLauncher.launch(
                        arrayOf(
                            Manifest.permission.BLUETOOTH_CONNECT,
                            Manifest.permission.BLUETOOTH_SCAN
                        )
                    )
                } else {
                    permissionLauncher.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION))
                }
            }
            PermissionStep.LOCATION ->
                permissionLauncher.launch(arrayOf(
                    Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION
                ))
            PermissionStep.NOTIFICATIONS -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    permissionLauncher.launch(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
                }
            }
        }
    }
}
