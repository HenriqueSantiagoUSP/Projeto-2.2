#pragma once

/*
 * Gestures of the wearable's only button (docs/decisoes.md, "Gestos do botão"):
 *
 *   - a double tap ("I'm fine"): two short presses, the second one starting soon after
 *     the first is released. Lying on the button after a fall holds it down, which never
 *     makes two taps, so this is the gesture that cancels an alert;
 *   - a hold ("I need help"): the button kept down for hold_ms. Reported while it is
 *     still down, so the LED can answer before the resident lets go.
 *
 * A single press means nothing. Plain C with no ESP-IDF: the button ISR only reports
 * that an edge happened (bg_on_edge), and comm_task feeds the level it reads once the
 * line has been quiet for settle_ms (debounce by settling). test_host/ runs it on the PC.
 *
 * Times are uint32_t milliseconds compared by unsigned difference, so the counter may
 * wrap around (every 49 days).
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BG_NONE,
    BG_DOUBLE_TAP,
    BG_HOLD,
} bg_gesture_t;

typedef struct {
    uint32_t settle_ms;    /* the level counts once it has been quiet this long after the last edge */
    uint32_t tap_max_ms;   /* a longer press is not a tap */
    uint32_t tap_gap_ms;   /* longest time from releasing the first tap to pressing the second */
    uint32_t hold_ms;      /* a press this long is a hold */
} bg_config_t;

#define BG_SETTLE_MS   30
#define BG_TAP_MAX_MS  1000

typedef struct {
    bg_config_t cfg;
    bool     pressed;       /* the settled level */
    bool     settling;      /* an edge was reported and the line has not been quiet for settle_ms yet */
    uint32_t edge_first_ms; /* first edge of the burst: when the transition happened */
    uint32_t edge_last_ms;  /* last edge of the burst: the settling counts from here */
    uint32_t press_ms;      /* when the current press began */
    uint32_t release_ms;    /* when the waiting tap was released */
    bool     tap_waiting;   /* one tap done, waiting for its pair */
    bool     hold_fired;    /* this press already gave BG_HOLD (or was down at boot) */
} button_gesture_t;

/* pressed_now: the level at boot. A button already down then must be released before it counts. */
void bg_init(button_gesture_t *g, const bg_config_t *cfg, bool pressed_now);

/* The ISR saw an edge at t_ms. Several edges of one press (bounce) may be reported. */
void bg_on_edge(button_gesture_t *g, uint32_t t_ms);

/* Call often, and by bg_ms_until_due at the latest, with the level read now. */
bg_gesture_t bg_tick(button_gesture_t *g, uint32_t now_ms, bool pressed_now);

/* How long the caller may wait before the next bg_tick; UINT32_MAX when nothing is due. */
uint32_t bg_ms_until_due(const button_gesture_t *g, uint32_t now_ms);
