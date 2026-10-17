package com.dashcue.core.database.dao

import androidx.room.*
import com.dashcue.core.database.entities.FavoriteDestination
import kotlinx.coroutines.flow.Flow

@Dao
interface FavoritesDao {
    @Query("SELECT * FROM favorite_destinations ORDER BY created_at DESC")
    fun getAllFavorites(): Flow<List<FavoriteDestination>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertFavorite(favorite: FavoriteDestination): Long

    @Delete
    suspend fun deleteFavorite(favorite: FavoriteDestination)

    @Query("DELETE FROM favorite_destinations WHERE id = :id")
    suspend fun deleteById(id: Long)
}
