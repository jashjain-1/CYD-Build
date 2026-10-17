package com.dashcue.core.database.entities

import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "favorite_destinations")
data class FavoriteDestination(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val name: String,
    val address: String,
    val latitude: Double,
    val longitude: Double,
    @ColumnInfo(name = "created_at")
    val createdAt: Long = System.currentTimeMillis()
)
