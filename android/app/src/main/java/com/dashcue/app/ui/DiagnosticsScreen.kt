package com.dashcue.app.ui

import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dashcue.app.DashCueViewModel

@Composable
fun DiagnosticsScreen(vm: DashCueViewModel) {
    val connection by vm.connectionState.collectAsStateWithLifecycle()
    val telemetry by vm.telemetry.collectAsStateWithLifecycle()
    val status by vm.deviceStatus.collectAsStateWithLifecycle()
    val hudSettings by vm.hudSettings.collectAsStateWithLifecycle()
    val log by vm.protocolLog.collectAsStateWithLifecycle()
    val context = LocalContext.current

    LazyColumn(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 16.dp)
    ) {
        item {
            DashCard {
                SectionLabel("Link")
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("State", color = DashCueColors.TextDim)
                    StatusPill(
                        connectionLabel(connection),
                        if (connectionIsHealthy(connection)) DashCueColors.Ok else DashCueColors.Warn
                    )
                }
                MetricRow("MTU negotiated", "${telemetry.mtu} B")
                MetricRow(
                    "RSSI",
                    if (telemetry.rssiDbm == 0) "not measured" else "${telemetry.rssiDbm} dBm"
                )
                MetricRow("Frames in / out", "${telemetry.packetsIn} / ${telemetry.packetsOut}")
                MetricRow("HUD settings echo", hudSettings?.let { "v${it.version}" } ?: "none")
            }
        }

        item {
            DashCard {
                SectionLabel("ESP32 telemetry")
                if (status == null) {
                    Text(
                        "No DeviceStatus received. Refresh while connected.",
                        color = DashCueColors.TextDim,
                        style = androidx.compose.material3.MaterialTheme.typography.bodySmall
                    )
                } else {
                    val ds = status!!
                    MetricRow("Uptime", "${ds.uptimeS} s")
                    MetricRow("Free heap", "${ds.minFreeHeap} B (min)")
                    MetricRow("Largest block", "${ds.largestFreeBlock} B")
                    MetricRow("Queue drops", ds.queueDrops.toString())
                    MetricRow("CRC failures", ds.crcFailures.toString())
                    MetricRow("Duplicate commands", ds.duplicateCommands.toString())
                    MetricRow("UI avg / p95 / max", "${ds.uiAvgMs} / ${ds.uiP95Ms} / ${ds.uiMaxMs} ms")
                    MetricRow("Safe mode", if (ds.safeMode) "yes" else "no")
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = vm::refreshDiagnostics,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = DashCueColors.Accent,
                            contentColor = DashCueColors.Bg
                        )
                    ) { Text("Request status") }
                    OutlinedButton(onClick = {
                        val payload = vm.exportDiagnostics()
                        val intent = Intent(Intent.ACTION_SEND).apply {
                            type = "application/json"
                            putExtra(Intent.EXTRA_SUBJECT, "DashCue diagnostic bundle")
                            putExtra(Intent.EXTRA_TEXT, payload)
                        }
                        context.startActivity(Intent.createChooser(intent, "Export diagnostics"))
                    }) { Text("Export bundle") }
                }
            }
        }

        item {
            DashCard {
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    SectionLabel("Protocol log")
                    OutlinedButton(onClick = vm::clearLog) { Text("Clear") }
                }
                Column(
                    Modifier
                        .fillMaxWidth()
                        .height(260.dp)
                        .background(DashCueColors.Bg, RoundedCornerShape(6.dp))
                        .padding(8.dp)
                ) {
                    if (log.isEmpty()) {
                        Text(
                            "No frames yet. Pair the HUD or request status.",
                            color = DashCueColors.TextDim,
                            fontSize = 11.sp
                        )
                    } else {
                        LazyColumn(verticalArrangement = Arrangement.spacedBy(1.dp)) {
                            itemsIndexed(log, key = { index, item -> "$index:${item.hashCode()}" }) { _, line ->
                                Text(
                                    line,
                                    color = logColor(line),
                                    fontFamily = FontFamily.Monospace,
                                    fontSize = 10.sp,
                                    maxLines = 2
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun logColor(line: String): Color = when {
    line.contains("FAILED") || line.contains("ERROR") -> DashCueColors.Danger
    line.contains("TX ") -> DashCueColors.Accent
    line.contains("LINK ") -> DashCueColors.Warn
    else -> DashCueColors.TextDim
}
