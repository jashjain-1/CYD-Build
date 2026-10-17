package com.dashcue.feature.media

import android.media.session.PlaybackState
import com.dashcue.v1.MediaAction
import org.junit.Assert.*
import org.junit.Test

class MediaCapabilityCalculatorTest {

    @Test
    fun testCapabilityBitmaskMapping() {
        // TC-AN-04: Mock diverse PlaybackState capability combinations
        // Case 1: Play + Pause only
        val actions1 = PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE
        val caps1 = MediaCapabilityCalculator.calculateCapabilities(actions1, supportsVolume = false)

        assertTrue(MediaCapabilityCalculator.isActionSupported(caps1, MediaAction.MA_PLAY))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps1, MediaAction.MA_PAUSE))
        assertFalse(MediaCapabilityCalculator.isActionSupported(caps1, MediaAction.MA_NEXT))
        assertFalse(MediaCapabilityCalculator.isActionSupported(caps1, MediaAction.MA_PREV))
        assertFalse(MediaCapabilityCalculator.isActionSupported(caps1, MediaAction.MA_SEEK_REL))

        // Case 2: Full controls: Play, Pause, Next, Prev, Seek
        val actions2 = PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or
                PlaybackState.ACTION_SKIP_TO_NEXT or PlaybackState.ACTION_SKIP_TO_PREVIOUS or
                PlaybackState.ACTION_SEEK_TO
        val caps2 = MediaCapabilityCalculator.calculateCapabilities(actions2, supportsVolume = true)

        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_PLAY))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_PAUSE))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_NEXT))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_PREV))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_SEEK_REL))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_SEEK_ABS))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps2, MediaAction.MA_VOL_STEP))

        // Case 3: Empty actions
        val caps3 = MediaCapabilityCalculator.calculateCapabilities(0L, supportsVolume = false)
        assertEquals(0, caps3)
        assertFalse(MediaCapabilityCalculator.isActionSupported(caps3, MediaAction.MA_PLAY))

        // Case 4: ACTION_PLAY_PAUSE maps to both MA_PLAY and MA_PAUSE
        val actions4 = PlaybackState.ACTION_PLAY_PAUSE
        val caps4 = MediaCapabilityCalculator.calculateCapabilities(actions4, supportsVolume = false)
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps4, MediaAction.MA_PLAY))
        assertTrue(MediaCapabilityCalculator.isActionSupported(caps4, MediaAction.MA_PAUSE))
        assertFalse(MediaCapabilityCalculator.isActionSupported(caps4, MediaAction.MA_NEXT))
    }
}
