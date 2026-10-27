package com.dashcue.app

import android.app.Application

class DashCueApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        AppState.init(this)
        // The HUD compass needs a heading even when the GPS receiver has no
        // course over ground (stationary, tunnel, cold start).
        HeadingProvider.start(this)
    }
}
