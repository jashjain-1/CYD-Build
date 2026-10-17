package com.dashcue.core.database.dao

import androidx.room.*
import com.dashcue.core.database.entities.RecentDestination
import kotlinx.coroutines.flow.Flow

@Dao
interface RecentsDao {
    @Query("SELECT * FROM recent_destinations ORDER BY visited_at DESC LIMIT 10")
    fun getRecentDestinations(): Flow<List<RecentDestination>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertRecent(recent: RecentDestination): Long

    @Query("DELETE FROM recent_destinations")
    suspend fun clearAll()
}
