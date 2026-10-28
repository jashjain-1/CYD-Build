#ifndef DASHCUE_UI_SCREEN_MEDIA_H
#define DASHCUE_UI_SCREEN_MEDIA_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "protocol/generated/dashcue.pb.h"

class UiScreenMedia {
public:
    UiScreenMedia();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();

    void update(const dashcue_v1_MediaState &state, bool is_stale);
    /** Advances the scrubber locally between phone updates. */
    void tick(uint32_t now_ms);

    /**
     * Chrome tint sink. The status bar and tab bar live outside this screen, so
     * the manager registers a callback instead of this class reaching into
     * them. Fires whenever the accent settles (create and track change), never
     * per animation frame.
     */
    using AccentListener = void (*)(uint32_t ui_accent_rgb, void *ctx);
    void setAccentListener(AccentListener cb, void *ctx);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_title_label;
    lv_obj_t *m_artist_label;
    lv_obj_t *m_app_badge;
    lv_obj_t *m_app_label;

    lv_obj_t *m_progress_bar;
    lv_obj_t *m_time_label;

    // Transient volume feedback pill auto-dismissing after 1.2 s
    lv_obj_t *m_volume_label;
    uint32_t m_volume_toast_until;
    lv_obj_t *m_stale_label;

    // Five driver-safe controls (>= 44 px): vol-, previous, play/pause,
    // next, vol+. Seeking is the scrubber itself.
    lv_obj_t *m_btn_prev;
    lv_obj_t *m_btn_play_pause;
    lv_obj_t *m_btn_next;

    lv_obj_t *m_play_pause_label;

    bool m_is_playing;
    int64_t m_duration_ms;
    uint32_t m_snapshot_version;
    int64_t m_position_ms;
    int64_t m_position_at_update_ms;
    float m_playback_speed;
    bool m_scrubbing;
    int64_t m_scrub_target_ms;
    int m_vol_btn_held;
    uint32_t m_vol_press_start_ms;
    uint32_t m_vol_last_step_ms;
    uint32_t m_volume_pct;
    bool m_muted;
    /** Phone link is stale: interpolation is frozen so the bar cannot run on. */
    bool m_stale;
    /** Previous track identity, used to evict artwork the moment it changes. */
    char m_last_title[64];
    char m_last_artist[64];
    char m_last_app[32];

    void renderProgress();
    void renderVolume();
    void sendVolumeStep(int dir, int step_pct = 5);
    /** Clean no-session view: no title, no scrubber fill, play disabled, no art. */
    void teardownMedia();
    /** Shows the transient volume pill over the artwork for ~1.2 s. */
    void showVolumeToast();
    /**
     * Retints discrete interactive accent surfaces from the album art dominant colour.
     * 1-shot widget updates without invalidating the screen background.
     */
    void applyAccent(uint32_t rgb, bool animate = false);
    /** Stamps accent colours directly onto foreground widgets. */
    void paintAccent();
    /** Raw album accent currently painted (art border). */
    uint32_t m_accent_rgb;
    /** Lifted accent actually used for small UI (35% toward white). */
    uint32_t m_accent_ui_rgb;

    AccentListener m_accent_listener;
    void *m_accent_ctx;

    static void onTransportClicked(lv_event_t *e);
    static void onProgressEvent(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_MEDIA_H
