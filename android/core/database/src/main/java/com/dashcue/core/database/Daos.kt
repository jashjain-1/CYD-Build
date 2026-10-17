package com.dashcue.core.database

import androidx.room.*
import kotlinx.coroutines.flow.Flow

@Dao
interface FavoriteDestinationDao {
    @Query("SELECT * FROM favorites ORDER BY createdAtMs DESC")
    fun getAll(): Flow<List<FavoriteDestination>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(destination: FavoriteDestination): Long

    @Delete
    suspend fun delete(destination: FavoriteDestination)

    @Query("DELETE FROM favorites WHERE id = :id")
    suspend fun deleteById(id: Long)
}

@Dao
interface RecentDestinationDao {
    @Query("SELECT * FROM recents ORDER BY visitedAtMs DESC LIMIT :limit")
    fun getRecents(limit: Int = 20): Flow<List<RecentDestination>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(destination: RecentDestination): Long

    @Query("DELETE FROM recents WHERE id NOT IN (SELECT id FROM recents ORDER BY visitedAtMs DESC LIMIT :limit)")
    suspend fun trimRecents(limit: Int = 20)

    @Query("DELETE FROM recents")
    suspend fun clearAll()
}

@Dao
interface TripRecordDao {
    @Query("SELECT * FROM trips ORDER BY completedAtMs DESC")
    fun getAllTrips(): Flow<List<TripRecord>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(trip: TripRecord): Long

    @Query("DELETE FROM trips")
    suspend fun clearAll()
}
