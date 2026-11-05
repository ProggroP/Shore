// Shore -- grosse LECO-Ziffern wie bei Coast, aber minutengenau und mit dem,
// was man tagsueber braucht: oben eine Zeile mit Wettersymbol und Tagestief/
// -hoch, links und rechts neben den Ziffern Wochentag und Tag im Monat, unten
// die Schritte mit einem Fussabdruck-Symbol statt einer Beschriftung.
//
// Farben kommen aus Vorlagen (src/pkjs/custom-clay.js), jede Farbe laesst
// sich danach einzeln aendern. Gezeichnet wird einmal pro Minute und bei
// neuen Einstellungen oder Wetterdaten, alles in einem Layer.

#include <pebble.h>

// ---------------------------------------------------------------------------
// Diagnose
// ---------------------------------------------------------------------------

// Nur fuer Store-Screenshots: feste Uhrzeit und Schrittzahl, weil
// emu-set-time nach einer pypkjs-Verbindung nicht haelt und der Emulator
// keine Schritte zaehlt. Vor dem Release wieder auskommentieren.
// #define SHORE_DEMO

// ---------------------------------------------------------------------------
// Layout je Plattform
// ---------------------------------------------------------------------------

// Alle y-Werte sind Mittellinien bzw. Oberkanten der sichtbaren Glyphen, die
// Leerraeume der Schriften gleichen die *_PAD-Werte aus.
#ifdef PBL_PLATFORM_EMERY
  // 200 x 228, eckig
  #define WEATHER_CY       21   // Mitte der Wetterzeile
  #define HOUR_Y           42   // Oberkante der Stundenziffern
  #define MIN_Y           118   // Oberkante der Minutenziffern
  #define STEPS_CY        206   // Mitte der Schrittzeile
  #define SIDE_INSET       26   // Mitte von Wochentag/Tag ab Displayrand
  #define WEATHER_ICON     28   // muss zu tools/make_icons.py passen
  #define STEPS_ICON       22
  #define INFO_FONT        FONT_KEY_GOTHIC_24_BOLD
  #define INFO_PAD          8   // Leerraum ueber den Ziffern der Schrift
  #define INFO_H           17   // Ziffernhoehe
#else
  // 260 x 260, rund: die Zeilen oben und unten muessen in die Sehne passen
  #define WEATHER_CY       35
  #define HOUR_Y           66
  #define MIN_Y           142
  #define STEPS_CY        234
  #define SIDE_INSET       31
  #define WEATHER_ICON     34
  #define STEPS_ICON       26
  #define INFO_FONT        FONT_KEY_GOTHIC_28_BOLD
  #define INFO_PAD         10
  #define INFO_H           20
#endif

// LECO 1976 bei Punktgroesse 92: Ziffern rund 64 px hoch, ueber ihnen liegen
// TEXT_LIFT Pixel Leerraum der Schrift
#define DIGIT_H            64
#define TEXT_LIFT          28
#define ICON_GAP            5   // Symbol -> Text
#define SEP_GAP             3   // Abstand um den Schraegstrich der Temperatur

// ---------------------------------------------------------------------------
// Wetter
// ---------------------------------------------------------------------------

// Zustandscodes = Position in der Sprite-Leiste, muessen zu
// tools/make_icons.py und src/pkjs/index.js passen
#define COND_COUNT           12
#define COND_NONE           255

#define WEATHER_INTERVAL_MIN 30
#define WEATHER_STALE_SEC   (3 * 60 * 60)

#define INBOX_SIZE          512
#define OUTBOX_SIZE          64

// ---------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------

#define PKEY_SETTINGS         1
#define PKEY_WEATHER          2

#define LANG_EN               0
#define LANG_DE               1

// Neue Felder nur ans Ende haengen: das Struct wird roh gespeichert
typedef struct {
  GColor bg;
  GColor hour;
  GColor minute;
  GColor text;
  GColor accent;
  bool show_weather;
  bool show_date;
  bool show_steps;
  uint8_t language;
} Settings;

typedef struct {
  int16_t lo;
  int16_t hi;
  uint8_t cond;
  int32_t time;     // Zeitpunkt des Abrufs, fuer die Veraltet-Pruefung
} Weather;

static Settings s_settings;
static Weather s_weather = { .cond = COND_NONE };

static Window *s_window;
static Layer *s_canvas;
static GFont s_digit_font;
static GBitmap *s_weather_icons;
static GBitmap *s_steps_icon;

// Standard: Vorlage "Noir", weiss auf schwarz wie bei Coast
static void default_settings(void) {
  s_settings = (Settings) {
    .bg     = GColorBlack,
    .hour   = GColorWhite,
    .minute = GColorWhite,
    .text   = GColorLightGray,
    .accent = GColorWhite,
    .show_weather = true,
    .show_date    = true,
    .show_steps   = true,
    .language     = LANG_EN,
  };
}

// ---------------------------------------------------------------------------
// Hilfen
// ---------------------------------------------------------------------------

static int text_width(const char *s, GFont font) {
  return graphics_text_layout_get_content_size(s, font, GRect(0, 0, 260, 60),
           GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// Text links ab x so setzen, dass seine Ziffern mittig auf cy liegen
static void draw_info(GContext *ctx, const char *s, int x, int w, int cy,
                      GTextAlignment align) {
  graphics_draw_text(ctx, s, fonts_get_system_font(INFO_FONT),
                     GRect(x, cy - INFO_H / 2 - INFO_PAD, w, INFO_PAD + INFO_H + 12),
                     GTextOverflowModeFill, align, NULL);
}

// 2-Bit-Palette: die Deckung bleibt, nur die Farbe wird getauscht
static void tint(GBitmap *bmp, GColor color) {
  if (!bmp) return;
  GColor *pal = gbitmap_get_palette(bmp);
  if (!pal) return;
  for (int i = 0; i < 4; i++) {
    pal[i].argb = (pal[i].argb & 0xC0) | (color.argb & 0x3F);
  }
}

static void tint_icons(void) {
  tint(s_weather_icons, s_settings.accent);
  tint(s_steps_icon, s_settings.accent);
}

static void draw_icon(GContext *ctx, GBitmap *bmp, GRect src, GPoint at) {
  gbitmap_set_bounds(bmp, src);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(at.x, at.y, src.size.w, src.size.h));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

// ---------------------------------------------------------------------------
// Uhrzeit
// ---------------------------------------------------------------------------

static void draw_time(GContext *ctx, GRect b, const struct tm *now) {
  char hour[4], min[4];
  int h = now->tm_hour;
  if (clock_is_24h_style()) {
    snprintf(hour, sizeof(hour), "%02d", h % 24);
  } else {
    h %= 12;
    snprintf(hour, sizeof(hour), "%d", h == 0 ? 12 : h);
  }
  snprintf(min, sizeof(min), "%02d", now->tm_min % 60);

  graphics_context_set_text_color(ctx, s_settings.hour);
  graphics_draw_text(ctx, hour, s_digit_font,
                     GRect(0, HOUR_Y - TEXT_LIFT, b.size.w, DIGIT_H + TEXT_LIFT + 16),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, s_settings.minute);
  graphics_draw_text(ctx, min, s_digit_font,
                     GRect(0, MIN_Y - TEXT_LIFT, b.size.w, DIGIT_H + TEXT_LIFT + 16),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------------------
// Wetterzeile: Symbol, Tief / Hoch
// ---------------------------------------------------------------------------

static void draw_weather(GContext *ctx, GRect b) {
  if (s_weather.cond >= COND_COUNT) return;
  if (time(NULL) - s_weather.time > WEATHER_STALE_SEC) return;

  GFont font = fonts_get_system_font(INFO_FONT);
  char lo[12], hi[12];
  snprintf(lo, sizeof(lo), "%d°", s_weather.lo);
  snprintf(hi, sizeof(hi), "%d°", s_weather.hi);
  const char *sep = "/";
  int lw = text_width(lo, font);
  int sw = text_width(sep, font);
  int hw = text_width(hi, font);

  // Symbol und Text als Block mittig
  int total = WEATHER_ICON + ICON_GAP + lw + SEP_GAP + sw + SEP_GAP + hw;
  int x = b.size.w / 2 - total / 2;

  draw_icon(ctx, s_weather_icons,
            GRect(s_weather.cond * WEATHER_ICON, 0, WEATHER_ICON, WEATHER_ICON),
            GPoint(x, WEATHER_CY - WEATHER_ICON / 2));
  x += WEATHER_ICON + ICON_GAP;

  graphics_context_set_text_color(ctx, s_settings.text);
  draw_info(ctx, lo, x, lw + 4, WEATHER_CY, GTextAlignmentLeft);
  x += lw + SEP_GAP;
  draw_info(ctx, sep, x, sw + 4, WEATHER_CY, GTextAlignmentLeft);
  x += sw + SEP_GAP;
  graphics_context_set_text_color(ctx, s_settings.accent);
  draw_info(ctx, hi, x, hw + 4, WEATHER_CY, GTextAlignmentLeft);
}

// ---------------------------------------------------------------------------
// Datum: Wochentag links, Tag im Monat rechts
// ---------------------------------------------------------------------------

static void draw_date(GContext *ctx, GRect b, const struct tm *now) {
  static const char *const days_en[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char *const days_de[7] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };
  const char *day = (s_settings.language == LANG_DE ? days_de : days_en)[now->tm_wday % 7];
  char num[4];
  snprintf(num, sizeof(num), "%d", now->tm_mday % 32);

  // Mittig in der Fuge zwischen Stunden und Minuten
  int cy = (HOUR_Y + DIGIT_H + MIN_Y) / 2;
  const int box = 2 * SIDE_INSET;

  graphics_context_set_text_color(ctx, s_settings.text);
  draw_info(ctx, day, 0, box, cy, GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, s_settings.accent);
  draw_info(ctx, num, b.size.w - box, box, cy, GTextAlignmentCenter);
}

// ---------------------------------------------------------------------------
// Schritte: Fussabdruck und Zahl
// ---------------------------------------------------------------------------

static void format_steps(char *buf, size_t size, int steps) {
  char sep = s_settings.language == LANG_DE ? '.' : ',';
  if (steps < 0) steps = 0;
  if (steps > 999999) steps = 999999;
  if (steps >= 1000) {
    snprintf(buf, size, "%d%c%03d", steps / 1000, sep, steps % 1000);
  } else {
    snprintf(buf, size, "%d", steps);
  }
}

static void draw_steps(GContext *ctx, GRect b) {
#ifdef SHORE_DEMO
  int steps = 8472;
#else
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(HealthMetricStepCount,
                                       time_start_of_today(), time(NULL));
  if (!(mask & HealthServiceAccessibilityMaskAvailable)) return;
  int steps = (int)health_service_sum_today(HealthMetricStepCount);
#endif

  char buf[12];
  format_steps(buf, sizeof(buf), steps);
  GFont font = fonts_get_system_font(INFO_FONT);
  int tw = text_width(buf, font);
  int total = STEPS_ICON + ICON_GAP + tw;
  int x = b.size.w / 2 - total / 2;

  draw_icon(ctx, s_steps_icon, GRect(0, 0, STEPS_ICON, STEPS_ICON),
            GPoint(x, STEPS_CY - STEPS_ICON / 2));
  graphics_context_set_text_color(ctx, s_settings.text);
  draw_info(ctx, buf, x + STEPS_ICON + ICON_GAP, tw + 4, STEPS_CY, GTextAlignmentLeft);
}

// ---------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  time_t t = time(NULL);
  struct tm *now = localtime(&t);
#ifdef SHORE_DEMO
  struct tm demo = *now;
  demo.tm_hour = 10;
  demo.tm_min = 8;
  now = &demo;
#endif

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, s_settings.bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_time(ctx, b, now);
  if (s_settings.show_weather) draw_weather(ctx, b);
  if (s_settings.show_date)    draw_date(ctx, b, now);
  if (s_settings.show_steps)   draw_steps(ctx, b);
}

// ---------------------------------------------------------------------------
// Ereignisse
// ---------------------------------------------------------------------------

static void request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

static void tick_handler(struct tm *now, TimeUnits changed) {
  if (s_settings.show_weather && now->tm_min % WEATHER_INTERVAL_MIN == 0) {
    request_weather();
  }
  layer_mark_dirty(s_canvas);
}

static void read_color(DictionaryIterator *iter, uint32_t key, GColor *out) {
  Tuple *t = dict_find(iter, key);
  if (t) *out = GColorFromHEX(t->value->int32);
}

// Toggles koennen als 1-Byte-Wert ankommen; int32 wuerde darueber hinauslesen
static void read_bool(DictionaryIterator *iter, uint32_t key, bool *out) {
  Tuple *t = dict_find(iter, key);
  if (t) *out = (t->length >= 4) ? (t->value->int32 != 0) : (t->value->uint8 != 0);
}

// Clay schickt Select-Werte als String
static void read_choice(DictionaryIterator *iter, uint32_t key, uint8_t *out) {
  Tuple *t = dict_find(iter, key);
  if (!t) return;
  *out = (t->type == TUPLE_CSTRING) ? (uint8_t)atoi(t->value->cstring)
                                    : (uint8_t)t->value->int32;
}

// Gleiche Vorder- und Hintergrundfarbe waere unlesbar -- Clay kann das nicht
// verhindern, also tut es die Uhr
static void enforce_contrast(GColor *fg) {
  if (gcolor_equal(*fg, s_settings.bg)) *fg = gcolor_legible_over(s_settings.bg);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  // Wetter
  Tuple *cond = dict_find(iter, MESSAGE_KEY_CONDITION);
  Tuple *lo   = dict_find(iter, MESSAGE_KEY_TEMP_LO);
  Tuple *hi   = dict_find(iter, MESSAGE_KEY_TEMP_HI);
  if (cond && lo && hi) {
    s_weather.cond = (uint8_t)cond->value->int32;
    s_weather.lo   = (int16_t)lo->value->int32;
    s_weather.hi   = (int16_t)hi->value->int32;
    s_weather.time = (int32_t)time(NULL);
    persist_write_data(PKEY_WEATHER, &s_weather, sizeof(s_weather));
    layer_mark_dirty(s_canvas);
  }

  // Einstellungen: nur wenn die Nachricht von Clay stammt
  if (!dict_find(iter, MESSAGE_KEY_COLOR_BG)) return;
  read_color(iter, MESSAGE_KEY_COLOR_BG,     &s_settings.bg);
  read_color(iter, MESSAGE_KEY_COLOR_HOUR,   &s_settings.hour);
  read_color(iter, MESSAGE_KEY_COLOR_MINUTE, &s_settings.minute);
  read_color(iter, MESSAGE_KEY_COLOR_TEXT,   &s_settings.text);
  read_color(iter, MESSAGE_KEY_COLOR_ACCENT, &s_settings.accent);
  enforce_contrast(&s_settings.hour);
  enforce_contrast(&s_settings.minute);
  enforce_contrast(&s_settings.text);
  enforce_contrast(&s_settings.accent);
  read_bool(iter, MESSAGE_KEY_SHOW_WEATHER, &s_settings.show_weather);
  read_bool(iter, MESSAGE_KEY_SHOW_DATE,    &s_settings.show_date);
  read_bool(iter, MESSAGE_KEY_SHOW_STEPS,   &s_settings.show_steps);
  read_choice(iter, MESSAGE_KEY_LANGUAGE, &s_settings.language);
  persist_write_data(PKEY_SETTINGS, &s_settings, sizeof(s_settings));
  tint_icons();
  layer_mark_dirty(s_canvas);
}

static void load_state(void) {
  default_settings();
  if (persist_exists(PKEY_SETTINGS)) {
    persist_read_data(PKEY_SETTINGS, &s_settings, sizeof(s_settings));
  }
  if (persist_exists(PKEY_WEATHER)) {
    persist_read_data(PKEY_WEATHER, &s_weather, sizeof(s_weather));
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  load_state();
  s_digit_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LECO_92));
  s_weather_icons = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER);
  s_steps_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_STEPS);
  tint_icons();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received);
  AppMessageResult res = app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  if (res != APP_MSG_OK) APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open: %d", (int)res);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  gbitmap_destroy(s_weather_icons);
  gbitmap_destroy(s_steps_icon);
  fonts_unload_custom_font(s_digit_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
