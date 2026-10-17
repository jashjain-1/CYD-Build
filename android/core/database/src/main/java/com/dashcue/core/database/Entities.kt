package com.dashcue.core.database

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "favorites")
data class FavoriteDestination(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val label: String,
    val address: String,
    val latitude: Double,
    val longitude: Double,
    val createdAtMs: Long = System.currentTimeMillis()
)

@Entity(tableName = "recents")
data class RecentDestination(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val label: String,
    val address: String,
    val latitude: Double,
    val longitude: Double,
    val visitedAtMs: Long = System.currentTimeMillis()
)

@Entity(tableName = "trips")
data class TripRecord(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val routeId: Long,
    val destinationLabel: String,
    val distanceM: Double,
    val durationS: Double,
    val completedAtMs: Long = System.currentTimeMillis()
)
