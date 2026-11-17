#include "button_gesture.h"

#include <string.h>

/* Milliseconds from then to now; correct across the wrap of the 32-bit counter. */
static uint32_t elapsed(uint32_t now, uint32_t then){
    return now - then;
}

void bg_init(button_gesture_t *g, const bg_config_t *cfg, bool pressed_now){
    memset(g, 0, sizeof(*g));
    g->cfg = *cfg;
    g->pressed = pressed_now;
    /* Down at boot: maybe stuck, or leaned on. It must be released before it can be a hold. */
    g->hold_fired = pressed_now;
}

void bg_on_edge(button_gesture_t *g, uint32_t t_ms){
    if (!g->settling) {
        g->settling = true;
        g->edge_first_ms = t_ms;
    }
    g->edge_last_ms = t_ms;
}

/* The settled level changed at t. Returns the gesture a release completes, if any. */
static bg_gesture_t on_transition(button_gesture_t *g, bool pressed, uint32_t t){
    g->pressed = pressed;
    if (pressed) {
        if (g->tap_waiting && elapsed(t, g->release_ms) > g->cfg.tap_gap_ms) {
            g->tap_waiting = false;   /* too late to pair with the earlier tap */
        }
        g->press_ms = t;
        g->hold_fired = false;
        return BG_NONE;
    }

    uint32_t duration = elapsed(t, g->press_ms);
    if (g->hold_fired || duration > g->cfg.tap_max_ms) {
        g->tap_waiting = false;       /* the end of a hold, or a press too long for a tap */
        return BG_NONE;
    }
    if (g->tap_waiting) {
        g->tap_waiting = false;       /* a third tap starts a new count */
        return BG_DOUBLE_TAP;
    }
    g->tap_waiting = true;
    g->release_ms = t;
    return BG_NONE;
}

bg_gesture_t bg_tick(button_gesture_t *g, uint32_t now_ms, bool pressed_now){
    if (g->settling) {
        if (elapsed(now_ms, g->edge_last_ms) < g->cfg.settle_ms) {
            /*
             * Still bouncing. Neither the hold nor the end of the tap pairing is decided
             * here: the edge being settled may be the release that cuts the hold short, or
             * the second tap arriving in time.
             */
            return BG_NONE;
        }
        g->settling = false;
        if (pressed_now != g->pressed) {
            bg_gesture_t gesture = on_transition(g, pressed_now, g->edge_first_ms);
            if (gesture != BG_NONE) {
                return gesture;
            }
        }
    }

    if (g->pressed && !g->hold_fired && elapsed(now_ms, g->press_ms) >= g->cfg.hold_ms) {
        g->hold_fired = true;
        g->tap_waiting = false;
        return BG_HOLD;
    }
    if (g->tap_waiting && !g->pressed && elapsed(now_ms, g->release_ms) > g->cfg.tap_gap_ms) {
        g->tap_waiting = false;       /* a single tap: nothing */
    }
    return BG_NONE;
}

uint32_t bg_ms_until_due(const button_gesture_t *g, uint32_t now_ms){
    if (g->settling) {
        uint32_t quiet = elapsed(now_ms, g->edge_last_ms);
        return quiet >= g->cfg.settle_ms ? 0 : g->cfg.settle_ms - quiet;
    }
    if (g->pressed && !g->hold_fired) {
        uint32_t held = elapsed(now_ms, g->press_ms);
        return held >= g->cfg.hold_ms ? 0 : g->cfg.hold_ms - held;
    }
    /* A waiting tap that expires gives nothing, so it never needs a timely tick. */
    return UINT32_MAX;
}
