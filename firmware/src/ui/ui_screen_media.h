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
    void refreshArtwork();

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
    lv_obj_t *m_artwork_box;
    lv_obj_t *m_artwork_icon;
    lv_obj_t *m_artwork_img;
    lv_image_dsc_t m_artwork_dsc;
    uint32_t m_artwork_generation;
    lv_obj_t *m_title_label;
    lv_obj_t *m_artist_label;
    lv_obj_t *m_app_badge;
    lv_obj_t *m_app_label;

    lv_obj_t *m_progress_bar;
    lv_obj_t *m_time_label;

    // Volume feedback. The stacked volume row is gone (the centered layout has
    // no room for it), so a transient pill over the artwork reports the level
    // for ~1.2 s after a ± press.
    lv_obj_t *m_volume_bar;    // unused, kept for ABI/source compatibility
    lv_obj_t *m_volume_icon;   // unused
    lv_obj_t *m_volume_label;  // the transient volume pill
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
    /** Clean no-session view: no title, no scrubber fill, play disabled, no art. */
    void teardownMedia();
    /** Shows the transient volume pill over the artwork for ~1.2 s. */
    void showVolumeToast();
    /**
     * Retints every accent surface from the album art's dominant colour.
     *
     * When [animate] is set and a gradient is already painted, the three colour
     * pairs (raw border, gradient top, lifted UI accent) crossfade over 300 ms so
     * a track change morphs the backdrop instead of snapping it.
     */
    void applyAccent(uint32_t rgb, bool animate = true);
    /** Stamps the three painted colours onto the widgets. */
    void paintAccent();
    /** Raw album accent currently painted (art border). */
    uint32_t m_accent_rgb;
    /** Lifted accent actually used for small UI (35% toward white). */
    uint32_t m_accent_ui_rgb;
    /** Gradient top colour currently painted. 0 = nothing painted yet. */
    uint32_t m_grad_top_rgb;
    uint32_t m_anim_raw_from, m_anim_raw_to;
    uint32_t m_anim_top_from, m_anim_top_to;
    uint32_t m_anim_ui_from, m_anim_ui_to;
    bool m_anim_active;
    static void onAccentAnim(void *var, int32_t value);

    AccentListener m_accent_listener;
    void *m_accent_ctx;

    static void onTransportClicked(lv_event_t *e);
    static void onProgressEvent(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_MEDIA_H
