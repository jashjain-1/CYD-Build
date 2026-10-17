package com.dashcue.core.database

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase

@Database(
    entities = [FavoriteDestination::class, RecentDestination::class, TripRecord::class],
    version = 1,
    exportSchema = false
)
abstract class DashCueDatabase : RoomDatabase() {
    abstract fun favoriteDestinationDao(): FavoriteDestinationDao
    abstract fun recentDestinationDao(): RecentDestinationDao
    abstract fun tripRecordDao(): TripRecordDao

    companion object {
        @Volatile
        private var INSTANCE: DashCueDatabase? = null

        fun getInstance(context: Context): DashCueDatabase {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: Room.databaseBuilder(
                    context.applicationContext,
                    DashCueDatabase::class.java,
                    "dashcue.db"
                ).fallbackToDestructiveMigration().build().also { INSTANCE = it }
            }
        }
    }
}
