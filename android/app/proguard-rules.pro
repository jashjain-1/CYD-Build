# DashCue ProGuard / R8 rules
-keep class com.google.protobuf.** { *; }
-keep class com.dashcue.v1.** { *; }
-keep class * extends androidx.room.RoomDatabase
-dontwarn com.google.protobuf.**
