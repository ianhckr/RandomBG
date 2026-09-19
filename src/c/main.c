#include <pebble.h>

// ---------------------------------------------------------------------------
// Configuration defaults (overridden at runtime via AppMessage)
// ---------------------------------------------------------------------------

#define NUM_BACKGROUNDS    8
#define BG_CHANGE_MINUTES  1   // default cadence when mode is TIME

#define DISPLAY_W  200
#define DISPLAY_H  228

#define BAR_H      46
#define BAR_Y      (DISPLAY_H - BAR_H)
#define BAR_CORNER 4

#define TIME_X   6
#define TIME_Y   (BAR_Y + 2)
#define TIME_W   118
#define TIME_H   40

#define DATE_X   (DISPLAY_W - 88)
#define DATE_W   82
#define DATE_Y   (BAR_Y + 14)
#define DATE_H   26

#define BATT_BAR_H    4
#define BATT_BAR_Y    (DISPLAY_H - BATT_BAR_H)
#define BATT_LOW_PCT  15
#define BATT_WARN_PCT 30

// ---------------------------------------------------------------------------
// AppMessage keys — must match js/app.js and settings page
// ---------------------------------------------------------------------------

#define MSG_KEY_BG_MODE      0   // uint8: 0 = time-based, 1 = tap/flick
#define MSG_KEY_BG_MINUTES   1   // uint8: minutes between changes (time mode)

// ---------------------------------------------------------------------------
// Background change mode
// ---------------------------------------------------------------------------

typedef enum {
    BG_MODE_TIME = 0,   // change on minute tick cadence
    BG_MODE_TAP  = 1,   // change on wrist tap / flick
} BgMode;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static Window   *s_window;
static Layer    *s_bg_draw_layer;
static Layer    *s_overlay_layer;
static GBitmap  *s_bg_bitmap;
static uint8_t   s_current_bg_index;

static char s_time_buf[8];
static char s_date_buf[16];

static GFont s_font_time;
static GFont s_font_date;

static BatteryChargeState s_battery_state;

// Persistent settings (defaults applied in init)
static BgMode  s_bg_mode    = BG_MODE_TIME;
static uint8_t s_bg_minutes = BG_CHANGE_MINUTES;

static const uint32_t s_bg_resource_ids[NUM_BACKGROUNDS] = {
    RESOURCE_ID_BG_0,
    RESOURCE_ID_BG_1,
    RESOURCE_ID_BG_2,
    RESOURCE_ID_BG_3,
    RESOURCE_ID_BG_4,
    RESOURCE_ID_BG_5,
    RESOURCE_ID_BG_6,
    RESOURCE_ID_BG_7,
};

// ---------------------------------------------------------------------------
// Persistence keys (survive reboots)
// ---------------------------------------------------------------------------

#define PERSIST_KEY_BG_MODE    100
#define PERSIST_KEY_BG_MINUTES 101

static void prefs_load(void) {
    if (persist_exists(PERSIST_KEY_BG_MODE)) {
        s_bg_mode = (BgMode)persist_read_int(PERSIST_KEY_BG_MODE);
    }
    if (persist_exists(PERSIST_KEY_BG_MINUTES)) {
        s_bg_minutes = (uint8_t)persist_read_int(PERSIST_KEY_BG_MINUTES);
        if (s_bg_minutes < 1) s_bg_minutes = 1;
    }
}

static void prefs_save(void) {
    persist_write_int(PERSIST_KEY_BG_MODE,    (int)s_bg_mode);
    persist_write_int(PERSIST_KEY_BG_MINUTES, (int)s_bg_minutes);
}

// ---------------------------------------------------------------------------
// Background layer update proc
// ---------------------------------------------------------------------------

static void bg_layer_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    if (!s_bg_bitmap) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "bg_layer_update_proc: s_bg_bitmap is NULL");
        return;
    }

    GRect bmp_bounds = gbitmap_get_bounds(s_bg_bitmap);
    GRect dest = GRect(
        (DISPLAY_W - bmp_bounds.size.w) / 2,
        (DISPLAY_H - bmp_bounds.size.h) / 2,
        bmp_bounds.size.w,
        bmp_bounds.size.h
    );

    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_draw_bitmap_in_rect(ctx, s_bg_bitmap, dest);
}

// ---------------------------------------------------------------------------
// Overlay layer update proc
// ---------------------------------------------------------------------------

static void overlay_layer_update_proc(Layer *layer, GContext *ctx) {
    // Backing bar
    GRect bar_rect = GRect(0, BAR_Y, DISPLAY_W, BAR_H);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bar_rect, BAR_CORNER, GCornersTop);

    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx,
        GPoint(BAR_CORNER, BAR_Y),
        GPoint(DISPLAY_W - BAR_CORNER, BAR_Y));

    // Time
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx,
        s_time_buf,
        s_font_time,
        GRect(TIME_X, TIME_Y, TIME_W, TIME_H),
        GTextOverflowModeWordWrap,
        GTextAlignmentLeft,
        NULL);

    // Date
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx,
        s_date_buf,
        s_font_date,
        GRect(DATE_X, DATE_Y, DATE_W, DATE_H),
        GTextOverflowModeWordWrap,
        GTextAlignmentRight,
        NULL);

    // Battery trough
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx,
        GRect(0, BATT_BAR_Y, DISPLAY_W, BATT_BAR_H),
        0, GCornerNone);

    // Battery fill
    int fill_w = (DISPLAY_W * s_battery_state.charge_percent) / 100;

    GColor fill_color;
    if (s_battery_state.is_charging) {
        fill_color = GColorYellow;
    } else if (s_battery_state.charge_percent <= BATT_LOW_PCT) {
        fill_color = GColorRed;
    } else if (s_battery_state.charge_percent <= BATT_WARN_PCT) {
        fill_color = GColorYellow;
    } else {
        fill_color = GColorGreen;
    }

    if (fill_w > 0) {
        graphics_context_set_fill_color(ctx, fill_color);
        graphics_fill_rect(ctx,
            GRect(0, BATT_BAR_Y, fill_w, BATT_BAR_H),
            0, GCornerNone);
    }
}

// ---------------------------------------------------------------------------
// Background advance (shared by both modes)
// ---------------------------------------------------------------------------

static void advance_background(void) {
    uint8_t next;
    if (NUM_BACKGROUNDS <= 1) {
        next = 0;
    } else {
        do {
            next = (uint8_t)(rand() % NUM_BACKGROUNDS);
        } while (next == s_current_bg_index);
    }
    s_current_bg_index = next;

    if (s_bg_bitmap) {
        gbitmap_destroy(s_bg_bitmap);
        s_bg_bitmap = NULL;
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loading background index %d", s_current_bg_index);
    s_bg_bitmap = gbitmap_create_with_resource(s_bg_resource_ids[s_current_bg_index]);
    if (!s_bg_bitmap) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "gbitmap_create_with_resource returned NULL");
    }
    layer_mark_dirty(s_bg_draw_layer);
    layer_mark_dirty(s_overlay_layer);
}

// ---------------------------------------------------------------------------
// Tap handler (BG_MODE_TAP)
// ---------------------------------------------------------------------------

static void tap_handler(AccelAxisType axis, int32_t direction) {
    // Fire on any wrist flick/double-tap regardless of axis or direction
    advance_background();
}

// ---------------------------------------------------------------------------
// Tick handler (BG_MODE_TIME)
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    strftime(s_time_buf, sizeof(s_time_buf),
             clock_is_24h_style() ? "%H:%M" : "%I:%M",
             tick_time);
    strftime(s_date_buf, sizeof(s_date_buf), "%a %d", tick_time);

    if (s_bg_mode == BG_MODE_TIME &&
        tick_time->tm_min % s_bg_minutes == 0) {
        advance_background();
    } else {
        layer_mark_dirty(s_overlay_layer);
    }
}

// ---------------------------------------------------------------------------
// Apply mode — subscribe/unsubscribe tap service as needed
// ---------------------------------------------------------------------------

static void apply_bg_mode(void) {
    if (s_bg_mode == BG_MODE_TAP) {
        accel_tap_service_subscribe(tap_handler);
    } else {
        accel_tap_service_unsubscribe();
    }
}

// ---------------------------------------------------------------------------
// Battery callback
// ---------------------------------------------------------------------------

static void battery_callback(BatteryChargeState state) {
    s_battery_state = state;
    layer_mark_dirty(s_overlay_layer);
}

// ---------------------------------------------------------------------------
// AppMessage callbacks
// ---------------------------------------------------------------------------

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
    Tuple *mode_t    = dict_find(iter, MSG_KEY_BG_MODE);
    Tuple *minutes_t = dict_find(iter, MSG_KEY_BG_MINUTES);

    bool changed = false;

    if (mode_t) {
        BgMode new_mode = (BgMode)mode_t->value->uint8;
        if (new_mode != s_bg_mode) {
            s_bg_mode = new_mode;
            apply_bg_mode();
            changed = true;
        }
    }

    if (minutes_t) {
        uint8_t new_min = minutes_t->value->uint8;
        if (new_min < 1) new_min = 1;
        if (new_min != s_bg_minutes) {
            s_bg_minutes = new_min;
            changed = true;
        }
    }

    if (changed) {
        prefs_save();
        APP_LOG(APP_LOG_LEVEL_INFO, "Settings updated: mode=%d minutes=%d",
                (int)s_bg_mode, (int)s_bg_minutes);
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage dropped: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    APP_LOG(APP_LOG_LEVEL_DEBUG, "window bounds: %d x %d",
            bounds.size.w, bounds.size.h);

    s_font_time = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
    s_font_date = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

    s_bg_draw_layer = layer_create(bounds);
    layer_set_update_proc(s_bg_draw_layer, bg_layer_update_proc);
    layer_add_child(root, s_bg_draw_layer);

    s_overlay_layer = layer_create(bounds);
    layer_set_update_proc(s_overlay_layer, overlay_layer_update_proc);
    layer_add_child(root, s_overlay_layer);

    srand((unsigned int)time(NULL));
    s_current_bg_index = (uint8_t)(rand() % NUM_BACKGROUNDS);
    advance_background();

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(s_time_buf, sizeof(s_time_buf),
             clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
    strftime(s_date_buf, sizeof(s_date_buf), "%a %d", t);
    layer_mark_dirty(s_overlay_layer);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
    s_battery_state = battery_state_service_peek();

    // Apply saved mode (subscribes tap service if needed)
    apply_bg_mode();
}

static void window_unload(Window *window) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    accel_tap_service_unsubscribe();

    layer_destroy(s_overlay_layer);
    layer_destroy(s_bg_draw_layer);

    if (s_bg_bitmap) {
        gbitmap_destroy(s_bg_bitmap);
        s_bg_bitmap = NULL;
    }
}

// ---------------------------------------------------------------------------
// App entry point
// ---------------------------------------------------------------------------

static void init(void) {
    prefs_load();

    // AppMessage inbox large enough for two small keys
    app_message_open(64, 64);
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
}

static void deinit(void) {
    prefs_save();
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
