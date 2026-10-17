package com.dashcue.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Brightness6
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dashcue.app.DashCueViewModel
import com.dashcue.core.ble.ReconnectionStateMachine

@Composable
fun SettingsScreen(
    vm: DashCueViewModel,
    onPairRequested: () -> Unit
) {
    val settings by vm.settings.collectAsStateWithLifecycle()
    val hudSettings by vm.hudSettings.collectAsStateWithLifecycle()
    val connection by vm.connectionState.collectAsStateWithLifecycle()
    val connected = connection == ReconnectionStateMachine.State.READY
    var showCalibration by remember { mutableStateOf(false) }
    var osrm by remember(settings.osrmBaseUrl) { mutableStateOf(settings.osrmBaseUrl) }
    var nominatim by remember(settings.nominatimBaseUrl) { mutableStateOf(settings.nominatimBaseUrl) }

    Column(
        Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        DashCard {
            SectionLabel("HUD display")
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.Filled.Brightness6,
                    contentDescription = null,
                    tint = DashCueColors.Accent
                )
                Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                    Text("Brightness ${settings.brightnessPct}%", color = DashCueColors.Text)
                    Slider(
                        value = settings.brightnessPct.toFloat(),
                        onValueChange = { vm.setBrightness(it.toInt()) },
                        valueRange = 10f..100f,
                        steps = 8,
                        colors = SliderDefaults.colors(
                            thumbColor = DashCueColors.Accent,
                            activeTrackColor = DashCueColors.Accent,
                            inactiveTrackColor = DashCueColors.Surface2
                        )
                    )
                }
            }
            MetricRow(
                "HUD confirmed",
                hudSettings?.let { "${it.brightnessPct}% · v${it.version}" } ?: "waiting for HUD"
            )

            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(Modifier.weight(1f)) {
                    Text("Invert orientation", color = DashCueColors.Text)
                    Text(
                        "Rotates the HUD 180° for an upside-down mount.",
                        color = DashCueColors.TextDim,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                Switch(
                    checked = settings.orientation == 1,
                    onCheckedChange = { vm.setOrientation(if (it) 1 else 0) }
                )
            }
        }

        DashCard {
            SectionLabel("Units and theme")
            Text("Distance units", color = DashCueColors.TextDim, fontSize = 12.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = settings.units == 0,
                    onClick = { vm.setUnits(0) },
                    label = { Text("Metric") }
                )
                FilterChip(
                    selected = settings.units == 1,
                    onClick = { vm.setUnits(1) },
                    label = { Text("Imperial") }
                )
            }

            Text("HUD theme", color = DashCueColors.TextDim, fontSize = 12.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf("Auto", "Dark", "Light").forEachIndexed { index, label ->
                    FilterChip(
                        selected = settings.themeMode == index,
                        onClick = { vm.setTheme(index) },
                        label = { Text(label) }
                    )
                }
            }

            Text("Seek step", color = DashCueColors.TextDim, fontSize = 12.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = settings.seekStepS == 10,
                    onClick = { vm.setSeekStep(10) },
                    label = { Text("10 s") }
                )
                FilterChip(
                    selected = settings.seekStepS == 30,
                    onClick = { vm.setSeekStep(30) },
                    label = { Text("30 s") }
                )
            }
        }

        DashCard {
            SectionLabel("Routing services")
            Text(
                "No public endpoint is bundled (DEC-14). Enter the OSRM server you operate " +
                    "and the Nominatim instance you are allowed to query.",
                color = DashCueColors.TextDim,
                style = MaterialTheme.typography.bodySmall
            )
            OutlinedTextField(
                value = osrm,
                onValueChange = { osrm = it },
                label = { Text("OSRM base URL") },
                placeholder = { Text("http://192.168.1.10:5000") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            OutlinedTextField(
                value = nominatim,
                onValueChange = { nominatim = it },
                label = { Text("Nominatim base URL") },
                placeholder = { Text("https://nominatim.example.org") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        vm.setOsrmEndpoint(osrm)
                        vm.setNominatimEndpoint(nominatim)
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = DashCueColors.Accent,
                        contentColor = DashCueColors.Bg
                    )
                ) { Text("Save endpoints") }
                OutlinedButton(onClick = {
                    osrm = ""
                    nominatim = ""
                    vm.setOsrmEndpoint("")
                    vm.setNominatimEndpoint("")
                }) { Text("Clear") }
            }
        }

        DashCard {
            SectionLabel("Device")
            StatusPill(
                if (connected) "HUD connected" else "HUD not connected",
                if (connected) DashCueColors.Ok else DashCueColors.TextDim
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                // Pairing is only meaningful before a bond exists; once the HUD
                // is connected, re-opening a pairing window cannot pair it and
                // only drops the live link into a confusing state.
                Button(
                    onClick = onPairRequested,
                    enabled = !connected,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = DashCueColors.Accent,
                        contentColor = DashCueColors.Bg,
                        disabledContainerColor = DashCueColors.Surface2,
                        disabledContentColor = DashCueColors.TextDim
                    )
                ) { Text(if (connected) "Paired" else "Pair HUD") }
                OutlinedButton(onClick = { vm.forgetPairedDevice() }) {
                    Text("Unpair")
                }
            }
            Text(
                "Unpairing also disconnects the foreground service. The HUD keeps its bond " +
                    "until it is forgotten on the device.",
                color = DashCueColors.TextDim,
                style = MaterialTheme.typography.bodySmall
            )
            OutlinedButton(
                onClick = { showCalibration = true },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Calibrate touch screen") }
        }
    }

    if (showCalibration) {
        AlertDialog(
            onDismissRequest = { showCalibration = false },
            title = { Text("Touch calibration") },
            text = {
                Text(
                    "Calibration runs on the HUD itself so the five targets are measured on " +
                        "the panel glass. On the HUD open More → Touch Calibration and follow " +
                        "the on-screen crosses. Calibration is stored on the device; the phone " +
                        "only needs to wait for it to finish."
                )
            },
            confirmButton = {
                TextButton(onClick = { showCalibration = false }) { Text("Got it") }
            }
        )
    }
}
