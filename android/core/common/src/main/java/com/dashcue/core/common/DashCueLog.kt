package com.dashcue.core.common

import android.util.Log

object DashCueLog {
    private const val TAG_PREFIX = "DashCue_"

    var isDebug: Boolean = false

    fun d(tag: String, msg: String) {
        if (isDebug) {
            Log.d("$TAG_PREFIX$tag", msg)
        }
    }

    fun i(tag: String, msg: String) {
        Log.i("$TAG_PREFIX$tag", msg)
    }

    fun w(tag: String, msg: String, tr: Throwable? = null) {
        if (tr != null) {
            Log.w("$TAG_PREFIX$tag", msg, tr)
        } else {
            Log.w("$TAG_PREFIX$tag", msg)
        }
    }

    fun e(tag: String, msg: String, tr: Throwable? = null) {
        if (tr != null) {
            Log.e("$TAG_PREFIX$tag", msg, tr)
        } else {
            Log.e("$TAG_PREFIX$tag", msg)
        }
    }
}
