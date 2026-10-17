package com.dashcue.core.database.entities

import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "recent_destinations")
data class RecentDestination(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val address: String,
    val latitude: Double,
    val longitude: Double,
    @ColumnInfo(name = "visited_at")
    val visitedAt: Long = System.currentTimeMillis()
)
