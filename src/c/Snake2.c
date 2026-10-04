/*
 * Snake 2 Watchface – Nokia Snake 2 style for Pebble
 * Primary target: Pebble Time 2 (Emery)  200 x 228 colour
 * Also works on:  Pebble Time (Basalt)   144 x 168 colour
 *
 * Layout (Emery)
 *   [INFO BAR 32 px]  weather-icon  [temp]  [DD.MM.YY]  [batt%]
 *   |                                                          |
 *   |   snake game area + food items (full screen depth)       |
 *   |         ┌─────────────────────┐                         |
 *   |         │      14:30          │  ← LECO 42 clock         |
 *   |         └─────────────────────┘                         |
 *   |   snake game area + food items                           |
 *   [WEEKDAY BAR 36 px]  Mo Di [Mi] Do Fr Sa So
 */

#include <pebble.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════
   CONSTANTS
   ═══════════════════════════════════════════════════════════ */

#define GRID_CELL   10   /* pixels per grid cell              */
#define CELL_BODY    8   /* rendered snake-body size          */
#define CELL_HALF    4   /* CELL_BODY / 2                     */

#define INFO_H      26   /* info-bar height (px)              */
#define WEEK_H      28   /* weekday-bar height (px)           */
/* Note: clock half-height is not a compile-time constant – it depends on
   the user's chosen font and is stored in the runtime global s_clock_half
   (set by apply_fonts() once per config change). */

#define SNAKE_MAX  120
#define FOOD_MAX     4
#define FOOD_KINDS   8

#define GRID_W_MAX  25
#define GRID_H_MAX  25

/* Animation speed per step (ms) – also used for minute-step animation */
static const uint16_t SPEED_MS[5] = { 500, 340, 200, 110, 60 };

/* Segments added per food eaten – 4 levels */
static const uint8_t  GROWTH_PER_FOOD[4] = { 1, 2, 4, 8 };

/* AppMessage keys – must match package.json messageKeys order */
#define KEY_TEMPERATURE      10000
#define KEY_WEATHER_ICON     10001
#define KEY_COLOR_SCHEME     10002
#define KEY_LANGUAGE         10003
#define KEY_SNAKE_SKIN       10004
#define KEY_SNAKE_SPEED      10005
#define KEY_ITEM_SET         10006
#define KEY_CELSIUS          10007
#define KEY_SHOW_24H         10008
#define KEY_SHOW_WEATHER     10009
#define KEY_WEATHER_PROVIDER  10010
#define KEY_ANIM_EVERY_MINUTE 10011
#define KEY_CLOCK_FONT        10012
#define KEY_SNAKE_GROWTH      10013
#define KEY_STEPS_PER_MIN     10014

/* Animation duration when triggered (ms) */
#define ANIM_DURATION_MS  4000

/* Pixel clock digit slide animation */
#define CLOCK_ANIM_FRAMES  8
#define CLOCK_ANIM_MS     40

/* Persist keys */
#define PK_COLOR_SCHEME        1
#define PK_LANGUAGE            2
#define PK_SNAKE_SKIN          3
#define PK_SNAKE_SPEED         4
#define PK_ITEM_SET            5
#define PK_CELSIUS             6
#define PK_SHOW_24H            7
#define PK_SHOW_WEATHER        8
#define PK_TEMPERATURE         9
#define PK_WEATHER_ICON       10
#define PK_ANIM_EVERY_MINUTE  11
#define PK_CLOCK_FONT         12
#define PK_SNAKE_GROWTH       13
#define PK_STEPS_PER_MIN       14
/* Game-state persistence (snake position, food, PRNG seed) */
#define PK_GAME_META          20   /* len, dir, alive, grow, food[], rand */
#define PK_GAME_BODY0         21   /* body[0..59]  – 240 bytes           */
#define PK_GAME_BODY1         22   /* body[60..119] – 240 bytes          */

/* Packed struct that fits in one 256-byte persist slot */
/* (defined after Vec2 / Food below) */

/* ═══════════════════════════════════════════════════════════
   DATA TYPES
   ═══════════════════════════════════════════════════════════ */

typedef struct { int16_t x, y; } Vec2;

typedef struct {
    Vec2  body[SNAKE_MAX];
    int   len;
    Vec2  dir;
    bool  alive;
    int   grow;       /* pending growth steps */
} Snake;

typedef struct {
    Vec2    pos;
    uint8_t kind;
    bool    active;
} Food;

/* Packed game-state struct that fits in one 256-byte persist slot */
typedef struct __attribute__((packed)) {
    int16_t  len;
    int16_t  grow;
    Vec2     dir;
    uint8_t  alive;
    uint32_t rand;
    Food     food[FOOD_MAX];
} GameMeta;

/* All user-configurable settings.  Loaded from flash on startup;
   updated via AppMessage whenever the Clay settings UI is saved. */
typedef struct {
    uint8_t color_scheme;      /* 0=Classic 1=Matrix 2=Ocean 3=Sunset 4=White 5=Auto */
    uint8_t language;          /* 0=DE 1=EN 2=FR 3=ES 4=IT                           */
    uint8_t snake_skin;        /* 0=Nokia classic 1=Viper 2=Python 3=Ice 4=Snake2 5=Neon */
    uint8_t snake_speed;       /* 0=very slow … 4=turbo  (index into SPEED_MS[])     */
    uint8_t snake_growth;      /* 0=+1  1=+2  2=+4  3=+8 segments per food eaten     */
    uint8_t steps_per_min;     /* 0=1  1=2  2=3  3=5  4=8 steps per minute tick      */
    uint8_t item_set;          /* 0=fruits 1=nature 2=mixed                          */
    bool    celsius;           /* true = °C, false = °F                              */
    bool    show_24h;          /* true = 24 h clock, false = 12 h with AM/PM         */
    bool    show_weather;      /* show weather icon + temperature in the info bar     */
    bool    anim_every_minute; /* debug: trigger animation on every minute tick       */
    uint8_t clock_font;        /* 0=LECO36  1=LECO42  2=Roboto49  3=FiraSans48  4=FiraSans60(Emery) */
} Config;

typedef struct {
    GColor bg;
    GColor s1, s2;         /* snake body / scale accent            */
    GColor shead, seye;    /* head colour, eye                     */
    GColor stongue;
    GColor box_bg, box_fg; /* info-box background / foreground     */
    GColor clock_fg;       /* clock digit colour                   */
    GColor day_bg, day_fg; /* active weekday box                   */
    GColor day_off;        /* inactive weekday text                */
} Scheme;

/* ═══════════════════════════════════════════════════════════
   GLOBALS
   ═══════════════════════════════════════════════════════════ */

static Window    *s_win;
static Layer     *s_canvas;
static AppTimer  *s_clock_anim_timer;
static AppTimer  *s_step_anim_timer;   /* drives per-minute animated steps */
static uint8_t    s_steps_remaining;   /* steps left to animate this minute */
static Food       s_food[FOOD_MAX];
static Config     s_cfg;
static Scheme     s_cs;

static int8_t     s_temp;
static uint8_t    s_wx;
static bool       s_wx_valid;

static BatteryChargeState s_batt;
static struct tm  s_tm;

static int        s_sw, s_sh;   /* screen size             */
static int        s_gw, s_gh;   /* grid cols / rows        */
static int        s_clock_cy;   /* clock centre-Y (pixels) */
static int        s_gy_min;     /* first valid snake row (below info bar)   */
static int        s_gy_max;     /* last  valid snake row (above week bar)   */

/* BFS work buffers */
static int16_t    s_bfs_par[GRID_W_MAX * GRID_H_MAX];
static uint16_t   s_bfs_q  [GRID_W_MAX * GRID_H_MAX];

static uint32_t   s_rand;

static GFont      s_fnt_clock;
static GFont      s_fnt_clock_custom; /* != NULL when a custom font is loaded */
static GFont      s_fnt_small;
static int        s_clock_half;  /* half-height of clock text (runtime) */

/* Pixel clock digit slide animation state */
static AppTimer  *s_settings_apply_timer;

static Snake      s_snake;
static bool       s_game_initialized; /* true after first spawn; survives window_load/unload cycles */
static int        s_clock_anim_frame;
static uint8_t    s_clock_anim_old[4]; /* previous H1,H2,M1,M2 digits */
static uint8_t    s_clock_anim_mask;   /* which digits changed (bits 0-3) */

/* ═══════════════════════════════════════════════════════════
   PRNG
   ═══════════════════════════════════════════════════════════ */
static uint32_t prng(void) {
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return s_rand;
}
static int prng_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(prng() % (uint32_t)(hi - lo + 1));
}

/* ═══════════════════════════════════════════════════════════
   GEOMETRY HELPERS
   ═══════════════════════════════════════════════════════════ */
static inline int   gidx(int x, int y) { return y * s_gw + x; }
static inline bool  in_grid(int x, int y) {
    return x >= 0 && x < s_gw && y >= 0 && y < s_gh;
}
/* Valid cell for both snake movement and food placement */
static inline bool  in_play_zone(int x, int y) {
    return x >= 0 && x < s_gw && y >= s_gy_min && y <= s_gy_max;
}
static inline GPoint cell_center(Vec2 v) {
    return GPoint(v.x * GRID_CELL + GRID_CELL/2,
                  v.y * GRID_CELL + GRID_CELL/2);
}
static inline bool  veq(Vec2 a, Vec2 b) { return a.x==b.x && a.y==b.y; }
static inline Vec2  vadd(Vec2 a, Vec2 b) { return (Vec2){a.x+b.x, a.y+b.y}; }

/* ═══════════════════════════════════════════════════════════
   CONFIG / PERSIST
   ═══════════════════════════════════════════════════════════ */

/* Read all settings from flash storage into s_cfg.
   Boolean fields (celsius, show_24h, show_weather, anim_every_minute) use
   persist_exists() to distinguish a first-run (no saved value) from an
   explicit false, so sensible defaults are applied rather than silently
   disabling weather or 24 h mode on a fresh install. */
static void load_config(void) {
    s_cfg.color_scheme = (uint8_t)persist_read_int(PK_COLOR_SCHEME);
    s_cfg.language     = (uint8_t)persist_read_int(PK_LANGUAGE);
    s_cfg.snake_skin   = (uint8_t)persist_read_int(PK_SNAKE_SKIN);
    s_cfg.snake_speed  = (uint8_t)persist_read_int(PK_SNAKE_SPEED);
    s_cfg.snake_growth = (uint8_t)persist_read_int(PK_SNAKE_GROWTH);
    s_cfg.steps_per_min = (uint8_t)persist_read_int(PK_STEPS_PER_MIN);
    s_cfg.item_set     = (uint8_t)persist_read_int(PK_ITEM_SET);
    s_cfg.celsius     = persist_exists(PK_CELSIUS)
                        ? (bool)persist_read_int(PK_CELSIUS)      : true;
    s_cfg.show_24h    = persist_exists(PK_SHOW_24H)
                        ? (bool)persist_read_int(PK_SHOW_24H)     : true;
    s_cfg.show_weather      = persist_exists(PK_SHOW_WEATHER)
                            ? (bool)persist_read_int(PK_SHOW_WEATHER) : true;
    s_cfg.anim_every_minute = persist_exists(PK_ANIM_EVERY_MINUTE)
                            ? (bool)persist_read_int(PK_ANIM_EVERY_MINUTE) : false;
    s_cfg.clock_font   = (uint8_t)persist_read_int(PK_CLOCK_FONT);
    s_temp            = (int8_t) persist_read_int(PK_TEMPERATURE);
    s_wx              = (uint8_t)persist_read_int(PK_WEATHER_ICON);
    s_wx_valid        = persist_exists(PK_TEMPERATURE);
}

/* Persist the current snake + food state so it survives app termination
   (the OS kills the watchface when the user opens the system menu). */
static void save_game_state(void) {
    GameMeta meta;
    meta.len   = (int16_t)s_snake.len;
    meta.grow  = (int16_t)s_snake.grow;
    meta.dir   = s_snake.dir;
    meta.alive = s_snake.alive ? 1 : 0;
    meta.rand  = s_rand;
    for (int i = 0; i < FOOD_MAX; i++) meta.food[i] = s_food[i];
    persist_write_data(PK_GAME_META,  &meta,              sizeof(meta));
    persist_write_data(PK_GAME_BODY0, &s_snake.body[0],   60 * sizeof(Vec2));
    persist_write_data(PK_GAME_BODY1, &s_snake.body[60],  60 * sizeof(Vec2));
}

/* Restore previously saved game state.  Returns true on success.
   If no saved state exists (e.g. first boot) returns false so the
   caller can do a fresh spawn instead. */
static bool load_game_state(void) {
    if (!persist_exists(PK_GAME_META)) return false;
    GameMeta meta;
    persist_read_data(PK_GAME_META,  &meta,              sizeof(meta));
    persist_read_data(PK_GAME_BODY0, &s_snake.body[0],   60 * sizeof(Vec2));
    persist_read_data(PK_GAME_BODY1, &s_snake.body[60],  60 * sizeof(Vec2));
    s_snake.len   = meta.len;
    s_snake.grow  = meta.grow;
    s_snake.dir   = meta.dir;
    s_snake.alive = meta.alive != 0;
    s_rand        = meta.rand;
    for (int i = 0; i < FOOD_MAX; i++) s_food[i] = meta.food[i];
    return true;
}

/* ═══════════════════════════════════════════════════════════
   PIXEL CLOCK DATA  –  3×5 bitmap digits
   ═══════════════════════════════════════════════════════════ */

/* 3-column × 5-row bitmap for digits 0–9.
   Each byte encodes one row: bit 2 = left pixel, bit 1 = middle, bit 0 = right. */
static const uint8_t PXDIGIT[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, /* 0: XXX / X.X / X.X / X.X / XXX */
    {0x2, 0x6, 0x2, 0x2, 0x7}, /* 1: .X. / XX. / .X. / .X. / XXX */
    {0x7, 0x1, 0x7, 0x4, 0x7}, /* 2: XXX / ..X / XXX / X.. / XXX */
    {0x7, 0x1, 0x7, 0x1, 0x7}, /* 3: XXX / ..X / XXX / ..X / XXX */
    {0x5, 0x5, 0x7, 0x1, 0x1}, /* 4: X.X / X.X / XXX / ..X / ..X */
    {0x7, 0x4, 0x7, 0x1, 0x7}, /* 5: XXX / X.. / XXX / ..X / XXX */
    {0x7, 0x4, 0x7, 0x5, 0x7}, /* 6: XXX / X.. / XXX / X.X / XXX */
    {0x7, 0x1, 0x1, 0x1, 0x1}, /* 7: XXX / ..X / ..X / ..X / ..X */
    {0x7, 0x5, 0x7, 0x5, 0x7}, /* 8: XXX / X.X / XXX / X.X / XXX */
    {0x7, 0x5, 0x7, 0x1, 0x7}, /* 9: XXX / X.X / XXX / ..X / XXX */
};

/* Compute the integer scale so HH:MM spans almost the full screen width.
   Layout: 4 digit-glyphs (3 cols each) + 1 colon (1 col) + 4 gaps (1 col each)
   = 17 native pixel columns.  Side margins = 4 px each → available = s_sw − 8. */
static int px_clock_scale(void) {
    int avail = (s_sw > 0 ? s_sw : 200) - 8;
    int s = avail / 17;
    return s < 1 ? 1 : s;
}

/* ═══════════════════════════════════════════════════════════
   FONT LOADER
   ═══════════════════════════════════════════════════════════ */

/* Load the clock font matching s_cfg.clock_font and update s_clock_half.
   Cases 0-2 use built-in system fonts (no embedded resource needed).
   Cases 3-4 load custom FiraSans Bold resources for a larger, sharper clock.
   On Basalt, font 4 falls back to FiraSans 48 because FiraSans 60 does not
   fit the narrower 144 px screen.
   Must be called once from window_load and again whenever clock_font changes. */
static void apply_fonts(void) {
    /* Release any previously loaded custom font to avoid leaking memory */
    if (s_fnt_clock_custom) {
        fonts_unload_custom_font(s_fnt_clock_custom);
        s_fnt_clock_custom = NULL;
    }

    switch (s_cfg.clock_font) {
        case 0: /* XS – LECO 36 (system) */
            s_fnt_clock  = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
            s_clock_half = 23;
            break;
        case 1: /* S – LECO 42 (system) */
            s_fnt_clock  = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
            s_clock_half = 27;
            break;
        case 2: /* M – Roboto Bold 49 (system) */
            s_fnt_clock  = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
            s_clock_half = 31;
            break;
        case 3: /* L – FiraSans Bold 48 (custom, both platforms) */
            s_fnt_clock_custom = fonts_load_custom_font(
                                     resource_get_handle(RESOURCE_ID_CLOCK_FIRA_48));
            s_fnt_clock  = s_fnt_clock_custom;
            s_clock_half = 30;
            break;
        case 5: case 6: case 7: case 8: {
            /* Pixel art clock styles – digits are rendered as bitmaps;
               s_fnt_clock is not used for drawing but must be non-NULL
               so shadowed_text() is never accidentally called on it. */
            int ps = px_clock_scale();
            s_fnt_clock  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
            s_clock_half = (5 * ps) / 2 + 4;
            break;
        }
        default: /* 4 = XL – FiraSans Bold 60 (Emery) / 48 (Basalt) */
#ifdef RESOURCE_ID_CLOCK_FIRA_60
            s_fnt_clock_custom = fonts_load_custom_font(
                                     resource_get_handle(RESOURCE_ID_CLOCK_FIRA_60));
            s_clock_half = 37;
#else
            /* Basalt: FIRA_60 not available, use FIRA_48 instead */
            s_fnt_clock_custom = fonts_load_custom_font(
                                     resource_get_handle(RESOURCE_ID_CLOCK_FIRA_48));
            s_clock_half = 30;
#endif
            s_fnt_clock = s_fnt_clock_custom;
            break;
    }
}

/* ═══════════════════════════════════════════════════════════
   COLOR SCHEME BUILDER
   ═══════════════════════════════════════════════════════════ */

/* Returns which base scheme (0-4) should be used for AUTO mode
   based on the current hour:
   Night  00-05  → Classic (dark)
   Dawn   06-08  → Sunset  (warm orange)
   Day    09-17  → White   (bright)
   Dusk   18-20  → Sunset  (warm orange)
   Evening21-23  → Ocean   (deep blue) */
static uint8_t get_auto_scheme(void) {
    int h = s_tm.tm_hour;
    if (h >= 0  && h <  6)  return 0; /* Classic – dark night  */
    if (h >= 6  && h <  9)  return 3; /* Sunset  – warm dawn   */
    if (h >= 9  && h < 18)  return 4; /* White   – bright day  */
    if (h >= 18 && h < 21)  return 3; /* Sunset  – dusk        */
    return 2;                         /* Ocean   – evening     */
}

/* Populate the UI-facing fields of s_cs (bg, box_bg/fg, clock_fg,
   day_bg/fg/off) from the given base scheme index (0–4).
   Does NOT handle AUTO (scheme 5) – resolve that via get_auto_scheme()
   before calling this function.
   Snake skin colours (s1, s2, shead, seye, stongue) are set separately
   in build_scheme() and are independent of the UI scheme. */
static void apply_ui_scheme(uint8_t scheme) {
    switch (scheme) {
        case 1: /* Matrix */
            s_cs.bg      = GColorBlack;
            s_cs.box_bg  = GColorDarkGreen;
            s_cs.box_fg  = GColorWhite;
            s_cs.clock_fg= GColorBrightGreen;
            s_cs.day_bg  = GColorIslamicGreen;
            s_cs.day_fg  = GColorWhite;
            s_cs.day_off = GColorDarkGray;
            break;
        case 2: /* Ocean */
            s_cs.bg      = GColorBlack;
            s_cs.box_bg  = GColorOxfordBlue;
            s_cs.box_fg  = GColorWhite;
            s_cs.clock_fg= GColorCeleste;
            s_cs.day_bg  = GColorBlueMoon;
            s_cs.day_fg  = GColorWhite;
            s_cs.day_off = GColorDarkGray;
            break;
        case 3: /* Sunset */
            s_cs.bg      = GColorBlack;
            s_cs.box_bg  = GColorBulgarianRose;
            s_cs.box_fg  = GColorWhite;
            s_cs.clock_fg= GColorChromeYellow;
            s_cs.day_bg  = GColorRed;
            s_cs.day_fg  = GColorWhite;
            s_cs.day_off = GColorLightGray;
            break;
        case 4: /* White – bright day */
            s_cs.bg      = GColorWhite;
            s_cs.box_bg  = GColorDarkGray;
            s_cs.box_fg  = GColorWhite;
            s_cs.clock_fg= GColorBlack;
            s_cs.day_bg  = GColorDarkGray;
            s_cs.day_fg  = GColorWhite;
            s_cs.day_off = GColorDarkGray;
            break;
        default: /* 0 = Classic */
            s_cs.bg      = GColorBlack;
            s_cs.box_bg  = GColorBlack;
            s_cs.box_fg  = GColorWhite;
            s_cs.clock_fg= GColorWhite;
            s_cs.day_bg  = GColorBlack;
            s_cs.day_fg  = GColorWhite;
            s_cs.day_off = GColorDarkGray;
            break;
        case 6: /* White Day Dark – white bg, black boxes / white text */
            s_cs.bg      = GColorWhite;
            s_cs.box_bg  = GColorBlack;
            s_cs.box_fg  = GColorWhite;
            s_cs.clock_fg= GColorBlack;
            s_cs.day_bg  = GColorBlack;
            s_cs.day_fg  = GColorWhite;
            s_cs.day_off = GColorDarkGray;
            break;
    }
}

/* Recompute the full active colour scheme – both UI colours (via
   apply_ui_scheme) and snake skin colours – and sync the window
   background so no stale colour flickers during redraws.
   Call whenever color_scheme or snake_skin changes, and on every
   minute tick so Auto mode transitions at hour boundaries. */
static void build_scheme(void) {
    uint8_t ui = s_cfg.color_scheme;
    if (ui == 5) ui = get_auto_scheme();
    apply_ui_scheme(ui);
    /* Keep window background in sync so no blank frame flickers */
    if (s_win) window_set_background_color(s_win, s_cs.bg);
    switch (s_cfg.snake_skin) {
        case 1: /* Viper – dark emerald */
            s_cs.s1      = GColorMidnightGreen;
            s_cs.s2      = GColorJaegerGreen;
            s_cs.shead   = GColorJaegerGreen;
            s_cs.seye    = GColorYellow;
            s_cs.stongue = GColorRed;
            break;
        case 2: /* Python – golden brown */
            s_cs.s1      = GColorWindsorTan;
            s_cs.s2      = GColorBulgarianRose;
            s_cs.shead   = GColorChromeYellow;
            s_cs.seye    = GColorBlack;
            s_cs.stongue = GColorRed;
            break;
        case 3: /* Ice – teal/blue */
            s_cs.s1      = GColorTiffanyBlue;
            s_cs.s2      = GColorCobaltBlue;
            s_cs.shead   = GColorVividCerulean;
            s_cs.seye    = GColorWhite;
            s_cs.stongue = GColorFolly;
            break;
        case 4: /* Nokia Snake 2 – authentic monochrome pixel style */
            s_cs.s1      = GColorGreen;
            s_cs.s2      = GColorDarkGreen;
            s_cs.shead   = GColorGreen;
            s_cs.seye    = GColorBlack;
            s_cs.stongue = GColorBlack; /* no tongue in Nokia style */
            break;
        case 5: /* Neon – cyberpunk pink/purple */
            s_cs.s1      = GColorMagenta;
            s_cs.s2      = GColorVividViolet;
            s_cs.shead   = GColorShockingPink;
            s_cs.seye    = GColorWhite;
            s_cs.stongue = GColorChromeYellow;
            break;
        case 6: /* Nokia 3310 authentic – pointed nose, clean flat body */
            s_cs.s1      = GColorIslamicGreen;
            s_cs.s2      = GColorDarkGreen;
            s_cs.shead   = GColorGreen;
            s_cs.seye    = GColorBlack;
            s_cs.stongue = GColorIslamicGreen; /* no tongue – colour unused */
            break;
        default: /* Nokia Classic – green */
            s_cs.s1      = GColorIslamicGreen;
            s_cs.s2      = GColorDarkGreen;
            s_cs.shead   = GColorGreen;
            s_cs.seye    = GColorWhite;
            s_cs.stongue = GColorRed;
            break;
    }
}

/* ═══════════════════════════════════════════════════════════
   GRID ZONE HELPERS
   ═══════════════════════════════════════════════════════════ */

/* True if grid cell (gx,gy) is a valid food placement location.
   Mirrors the in_play_zone() constraint but compares pixel centres
   so tiny screens (Basalt) still get a usable food zone.
   Excludes the info bar (top), weekday bar (bottom), and a margin around
   the clock halo so food never spawns under the time display. */
static bool is_food_zone(int gx, int gy) {
    int py_c = gy * GRID_CELL + GRID_CELL/2;  /* cell Y centre */
    (void)gx;

    /* Must be below info bar */
    if (py_c <= INFO_H + 4) return false;
    /* Must be above weekday bar */
    if (py_c >= s_sh - WEEK_H - 4) return false;
    /* Must NOT overlap clock halo (centre ± s_clock_half + 4 margin) */
    if (py_c >= s_clock_cy - s_clock_half - 4 &&
        py_c <= s_clock_cy + s_clock_half + 4) return false;
    return true;
}

/* True if any snake body segment occupies grid cell (gx,gy).
   skip_last: exclude that many trailing segments from the check.
   Pass skip_last=1 during pathfinding so the tail tip (which will have
   moved by the time the head arrives) is treated as a free cell. */
static bool snake_at(int gx, int gy, int skip_last) {
    for (int i = 0; i < s_snake.len - skip_last; i++) {
        if (s_snake.body[i].x == gx && s_snake.body[i].y == gy) return true;
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════
   BFS PATHFINDING
   ═══════════════════════════════════════════════════════════ */

/* Cardinal movement vectors: right, left, down, up */
static const Vec2 DIRS4[4] = {{1,0},{-1,0},{0,1},{0,-1}};

/* BFS shortest-path search from `start` to `goal` within the play zone.
   Returns the unit direction vector of the first step on the optimal path,
   or {0,0} if no path exists (goal unreachable or arena fully blocked).
   Uses s_bfs_par[] as a parent-index array and s_bfs_q[] as the queue;
   both are pre-allocated globals sized for the largest supported grid. */
static Vec2 bfs_first_step(Vec2 start, Vec2 goal) {
    int n = s_gw * s_gh;
    for (int i = 0; i < n; i++) s_bfs_par[i] = -1;

    int head = 0, tail = 0;
    int si = gidx(start.x, start.y);
    s_bfs_par[si] = (int16_t)si;
    s_bfs_q[tail++] = (uint16_t)si;

    while (head < tail) {
        int ci = s_bfs_q[head++];
        int cx = ci % s_gw;
        int cy_g = ci / s_gw;

        if (cx == goal.x && cy_g == goal.y) {
            /* Trace back to first step */
            int idx = ci;
            while (s_bfs_par[idx] != (int16_t)si) {
                int prev = s_bfs_par[idx];
                if (prev == idx) break;
                idx = prev;
            }
            return (Vec2){ (int16_t)(idx % s_gw - start.x),
                           (int16_t)(idx / s_gw - start.y) };
        }

        for (int d = 0; d < 4; d++) {
            int nx = cx       + DIRS4[d].x;
            int ny = cy_g + DIRS4[d].y;
            if (!in_play_zone(nx, ny)) continue;
            int ni = gidx(nx, ny);
            if (s_bfs_par[ni] != -1) continue;
            if (snake_at(nx, ny, 1)) continue;
            s_bfs_par[ni] = (int16_t)ci;
            s_bfs_q[tail++] = (uint16_t)ni;
        }
    }
    return (Vec2){0, 0};
}

/* Determine the next movement direction for the autonomous snake AI.
 *
 * Strategy:
 *  1. Find the nearest active food item (Manhattan distance).
 *  2. Run BFS toward it for the shortest-path first step.
 *  3. With 30 % probability – or when BFS finds no path – pick a random
 *     valid non-reversing direction instead.  This avoids the perfectly
 *     mechanical look of pure pathfinding and makes the snake feel alive.
 *  4. Safety fallback: if the chosen step would hit a wall or self,
 *     scan all four directions for any valid escape route.
 */
static Vec2 compute_next_dir(void) {
    Vec2 head = s_snake.body[0];

    /* Find nearest active food by Manhattan distance */
    int best_d = 9999, best_i = -1;
    for (int i = 0; i < FOOD_MAX; i++) {
        if (!s_food[i].active) continue;
        int d = abs(s_food[i].pos.x - head.x) + abs(s_food[i].pos.y - head.y);
        if (d < best_d) { best_d = d; best_i = i; }
    }

    Vec2 bfs_dir = {0, 0};
    if (best_i >= 0) {
        bfs_dir = bfs_first_step(head, s_food[best_i].pos);
    }

    /* 70% follow BFS path, 30% random for organic feel */
    bool use_random = (prng() % 10 < 3) ||
                      (bfs_dir.x == 0 && bfs_dir.y == 0);
    Vec2 chosen = bfs_dir;

    if (use_random) {
        /* Shuffle DIRS4 and pick first valid non-reverse direction */
        int order[4] = {0,1,2,3};
        for (int i = 3; i > 0; i--) {
            int j = (int)(prng() % (uint32_t)(i+1));
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        Vec2 cur = s_snake.dir;
        for (int i = 0; i < 4; i++) {
            Vec2 d = DIRS4[order[i]];
            if (d.x == -cur.x && d.y == -cur.y) continue; /* no 180 reversal */
            Vec2 nxt = {head.x + d.x, head.y + d.y};
            if (!in_play_zone(nxt.x, nxt.y)) continue;
            if (snake_at(nxt.x, nxt.y, 1)) continue;
            chosen = d;
            break;
        }
    }

    /* Safety: validate chosen direction */
    Vec2 cur = s_snake.dir;
    if (chosen.x == -cur.x && chosen.y == -cur.y) chosen = cur;
    Vec2 nxt = {head.x + chosen.x, head.y + chosen.y};
    if (!in_play_zone(nxt.x, nxt.y) || snake_at(nxt.x, nxt.y, 1)) {
        for (int d = 0; d < 4; d++) {
            Vec2 nd = DIRS4[d];
            if (nd.x == -cur.x && nd.y == -cur.y) continue;
            Vec2 nn = {head.x + nd.x, head.y + nd.y};
            if (!in_play_zone(nn.x, nn.y)) continue;
            if (snake_at(nn.x, nn.y, 1)) continue;
            chosen = nd;
            goto dir_done;
        }
    }
dir_done:
    return chosen;
}

/* ═══════════════════════════════════════════════════════════
   FOOD MANAGEMENT
   ═══════════════════════════════════════════════════════════ */
/* Place a new food item in `slot` at a random unoccupied cell inside the
   food zone (neither UI bar, nor clock halo, nor another food, nor on the
   snake's body).  The item kind is chosen by s_cfg.item_set:
     0 = fruits  (kinds 0–4: apples, water drop, cherry)
     1 = nature  (kinds 4–7: cherry, mushroom, star, gem)
     2 = mixed   (kinds 0–7: all types)
   No-ops if no free candidate cells exist. */
static void place_food(int slot) {
    /* Collect all valid candidate cells */
    static uint16_t cands[GRID_W_MAX * GRID_H_MAX];
    int nc = 0;
    for (int y = 0; y < s_gh; y++) {
        for (int x = 0; x < s_gw; x++) {
            if (!is_food_zone(x, y)) continue;
            if (snake_at(x, y, 0)) continue;
            bool taken = false;
            for (int f = 0; f < FOOD_MAX; f++) {
                if (f == slot) continue;
                if (s_food[f].active &&
                    s_food[f].pos.x == x && s_food[f].pos.y == y) {
                    taken = true; break;
                }
            }
            if (!taken) cands[nc++] = (uint16_t)gidx(x, y);
        }
    }
    if (nc == 0) return;

    int pick = prng_range(0, nc - 1);
    int idx  = cands[pick];
    s_food[slot].pos.x = (int16_t)(idx % s_gw);
    s_food[slot].pos.y = (int16_t)(idx / s_gw);

    switch (s_cfg.item_set) {
        case 1: s_food[slot].kind = (uint8_t)prng_range(4, 7); break; /* nature */
        case 2: s_food[slot].kind = (uint8_t)prng_range(0, FOOD_KINDS-1); break;
        default: s_food[slot].kind = (uint8_t)prng_range(0, 4); break; /* fruits */
    }
    s_food[slot].active = true;
}

/* Deactivate all food slots and place a fresh item in each one. */
static void init_food(void) {
    for (int i = 0; i < FOOD_MAX; i++) {
        s_food[i].active = false;
        place_food(i);
    }
}

/* ═══════════════════════════════════════════════════════════
   SNAKE SPAWN
   ═══════════════════════════════════════════════════════════ */
/* Map tm_wday (Sun=0) to our weekday-bar index (0=Mon … 6=Sun). */
static int today_idx(void) {
    return s_tm.tm_wday == 0 ? 6 : s_tm.tm_wday - 1;
}

/* Spawn a fresh snake in the play zone.
   The head is placed at the bottom row of the play zone (s_gy_max),
   directly above today's weekday column, pointing upward.
   The snake starts with length 1 and accumulates pending growth in
   s_snake.grow so it elongates step-by-step from the very first tick
   rather than appearing fully formed. */
static void spawn_snake(void) {
    int day    = today_idx();
    int item_w = s_sw / 7;
    int start_px = day * item_w + item_w / 2;
    int start_gx = start_px / GRID_CELL;
    if (start_gx < 0)      start_gx = 0;
    if (start_gx >= s_gw)  start_gx = s_gw - 1;

    /* Head at the bottom row of the play zone, above today's weekday */
    int head_gy = s_gy_max;

    /* Random target length 4..8; snake starts as just the head
       and grows to full length step by step via s_snake.grow */
    int max_avail = head_gy - s_gy_min + 1;
    if (max_avail > 8) max_avail = 8;
    if (max_avail < 1) max_avail = 1;
    int min_target = (max_avail >= 4) ? 4 : 1;
    int target_len = prng_range(min_target, max_avail);

    s_snake.len     = 1;   /* start with head only */
    s_snake.body[0] = (Vec2){(int16_t)start_gx, (int16_t)head_gy};
    s_snake.dir     = (Vec2){0, -1}; /* heading upward */
    s_snake.alive   = true;
    s_snake.grow    = target_len - 1; /* grow to full length over first steps */
}

/* ═══════════════════════════════════════════════════════════
   SNAKE STEP
   ═══════════════════════════════════════════════════════════ */

/* Advance the snake exactly one grid cell per animation timer tick.
   Handles:
    - Respawn on death (collision with self, or Y boundary hit)
    - X-axis wrap (left edge → right edge and vice versa)
    - Y-axis death (hitting the info bar or weekday bar boundary)
    - Pending growth: s_snake.grow counts extra segments to add
    - Food eating: deactivates the food slot, applies growth, replaces food */
static void snake_step(void) {
    if (!s_snake.alive) {
        spawn_snake();
        init_food();
        layer_mark_dirty(s_canvas);
        return;
    }

    Vec2 dir      = compute_next_dir();
    s_snake.dir   = dir;
    Vec2 new_head = vadd(s_snake.body[0], dir);

    /* Wrap X at screen edges; clamp Y to play zone (hitting top/bottom = death) */
    if (new_head.x < 0)          new_head.x = (int16_t)(s_gw - 1);
    if (new_head.x >= s_gw)      new_head.x = 0;

    /* Clock zone warp: snake entering the clock halo exits from the opposite side */
    {
        int cgt = (s_clock_cy - s_clock_half - 4) / GRID_CELL;
        int cgb = (s_clock_cy + s_clock_half + 4) / GRID_CELL;
        if (new_head.y >= cgt && new_head.y <= cgb) {
            if (dir.y > 0 && cgb + 1 <= s_gy_max) {
                new_head.y = (int16_t)(cgb + 1);
            } else if (dir.y < 0 && cgt - 1 >= s_gy_min) {
                new_head.y = (int16_t)(cgt - 1);
            }
        }
    }

    if (new_head.y < s_gy_min || new_head.y > s_gy_max) {
        /* Hit a UI zone wall – die and respawn */
        s_snake.alive = false;
        layer_mark_dirty(s_canvas);
        return;
    }

    /* Self-collision */
    for (int i = 0; i < s_snake.len - 1; i++) {
        if (veq(s_snake.body[i], new_head)) {
            s_snake.alive = false;
            layer_mark_dirty(s_canvas);
            return;
        }
    }

    /* Check food */
    int eaten = -1;
    for (int f = 0; f < FOOD_MAX; f++) {
        if (s_food[f].active && veq(s_food[f].pos, new_head)) {
            eaten = f; break;
        }
    }

    /* Shift body */
    if (s_snake.grow > 0) {
        if (s_snake.len < SNAKE_MAX) {
            for (int i = s_snake.len; i > 0; i--) s_snake.body[i] = s_snake.body[i-1];
            s_snake.body[0] = new_head;
            s_snake.len++;
        } else {
            for (int i = s_snake.len-1; i > 0; i--) s_snake.body[i] = s_snake.body[i-1];
            s_snake.body[0] = new_head;
        }
        s_snake.grow--;
    } else {
        for (int i = s_snake.len-1; i > 0; i--) s_snake.body[i] = s_snake.body[i-1];
        s_snake.body[0] = new_head;
    }

    if (eaten >= 0) {
        s_food[eaten].active = false;
        uint8_t gi = s_cfg.snake_growth < 4 ? s_cfg.snake_growth : 3;
        s_snake.grow += GROWTH_PER_FOOD[gi];
        place_food(eaten);
    }

    layer_mark_dirty(s_canvas);
}

/* ═══════════════════════════════════════════════════════════
   DRAWING HELPERS
   ═══════════════════════════════════════════════════════════ */
/* Fill a single pixel at (x, y) – used by all pixel-art drawers. */
static inline void px1(GContext *ctx, int x, int y) {
    graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
}

/* Draw a rounded rectangle filled with `bg`, outlined in `fg`,
   with `text` centred using `fnt`.
   Used for the info-bar data boxes and the today highlight in the
   weekday bar. */
static void draw_box(GContext *ctx, GRect r, GColor bg, GColor fg,
                     const char *text, GFont fnt) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, r, 4, GCornersAll);
    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_round_rect(ctx, r, 4);
    graphics_context_set_text_color(ctx, fg);
    GRect inner = GRect(r.origin.x + 2, r.origin.y,
                        r.size.w - 4,   r.size.h - 2);
    graphics_draw_text(ctx, text, fnt, inner,
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

/* Draw text with a 1 px black drop-shadow offset (+1,+1) so labels
   remain legible over any background colour or snake body. */
static void shadowed_text(GContext *ctx, const char *t, GFont fnt,
                          GRect r, GColor c, GTextAlignment align) {
    graphics_context_set_text_color(ctx, GColorBlack);
    GRect sr = GRect(r.origin.x+1, r.origin.y+1, r.size.w, r.size.h);
    graphics_draw_text(ctx, t, fnt, sr, GTextOverflowModeFill, align, NULL);
    graphics_context_set_text_color(ctx, c);
    graphics_draw_text(ctx, t, fnt, r,  GTextOverflowModeFill, align, NULL);
}

/* ═══════════════════════════════════════════════════════════
   WEATHER ICONS  16 x 16 pixel art  (ox,oy = top-left)
   ═══════════════════════════════════════════════════════════ */
/* Returns `ocol` unless it's the GColorClear sentinel, else `normal`.
   Lets every icon part be repainted solid for the black outline pass. */
static inline GColor icol(GColor ocol, GColor normal) {
    return ocol.a == 0 ? normal : ocol;
}

static void wx_sun(GContext *ctx, int ox, int oy, GColor ocol) {
    graphics_context_set_fill_color(ctx, icol(ocol, GColorChromeYellow));
    graphics_fill_circle(ctx, GPoint(ox+7, oy+7), 4);
    const int8_t rx[] = {7,7,2,12, 3,11, 3,11};
    const int8_t ry[] = {1,13,7, 7, 3,11,11, 3};
    for (int i = 0; i < 8; i++) px1(ctx, ox+rx[i], oy+ry[i]);
}

static void wx_cloud_shape(GContext *ctx, int ox, int oy, GColor c, GColor ocol) {
    graphics_context_set_fill_color(ctx, icol(ocol, c));
    graphics_fill_rect(ctx, GRect(ox+2, oy+7, 12, 5), 2, GCornersAll);
    graphics_fill_circle(ctx, GPoint(ox+5, oy+6), 3);
    graphics_fill_circle(ctx, GPoint(ox+9, oy+5), 4);
}

static void wx_partly_cloudy(GContext *ctx, int ox, int oy, GColor ocol) {
    graphics_context_set_fill_color(ctx, icol(ocol, GColorChromeYellow));
    graphics_fill_circle(ctx, GPoint(ox+4, oy+8), 3);
    px1(ctx,ox+4,oy+4); px1(ctx,ox+4,oy+12);
    px1(ctx,ox+1,oy+8); px1(ctx,ox+7,oy+8);
    wx_cloud_shape(ctx, ox+1, oy-1, GColorWhite, ocol);
}

static void wx_rain(GContext *ctx, int ox, int oy, GColor ocol) {
    wx_cloud_shape(ctx, ox, oy, GColorLightGray, ocol);
    graphics_context_set_fill_color(ctx, icol(ocol, GColorBlueMoon));
    const int8_t rx2[] = {3,7,11,5,9};
    const int8_t ry2[] = {13,12,13,14,15};
    for (int i = 0; i < 5; i++) {
        px1(ctx, ox+rx2[i], oy+ry2[i]);
        if (ry2[i] > 0) px1(ctx, ox+rx2[i], oy+ry2[i]-1);
    }
}

static void wx_snow(GContext *ctx, int ox, int oy, GColor ocol) {
    wx_cloud_shape(ctx, ox, oy, GColorLightGray, ocol);
    graphics_context_set_fill_color(ctx, icol(ocol, GColorCeleste));
    const int8_t sx[] = {3,7,11,5,9};
    const int8_t sy[] = {13,12,13,14,15};
    for (int i = 0; i < 5; i++) {
        px1(ctx, ox+sx[i], oy+sy[i]);
        px1(ctx, ox+sx[i]-1, oy+sy[i]);
        px1(ctx, ox+sx[i]+1, oy+sy[i]);
    }
}

static void wx_storm(GContext *ctx, int ox, int oy, GColor ocol) {
    wx_cloud_shape(ctx, ox, oy, GColorDarkGray, ocol);
    graphics_context_set_fill_color(ctx, icol(ocol, GColorChromeYellow));
    px1(ctx,ox+9,oy+8); px1(ctx,ox+8,oy+9); px1(ctx,ox+9,oy+9);
    px1(ctx,ox+7,oy+10); px1(ctx,ox+8,oy+11); px1(ctx,ox+7,oy+12);
}

static void wx_fog(GContext *ctx, int ox, int oy, GColor ocol) {
    graphics_context_set_stroke_color(ctx, icol(ocol, GColorLightGray));
    graphics_context_set_stroke_width(ctx, 1);
    for (int l = 0; l < 5; l++) {
        int y  = oy + 3 + l * 2;
        int x0 = ox + (l%2 ? 2 : 0);
        int x1 = ox + 14 - (l%2 ? 2 : 0);
        graphics_draw_line(ctx, GPoint(x0,y), GPoint(x1,y));
    }
}

static void wx_night(GContext *ctx, int ox, int oy, GColor ocol) {
    graphics_context_set_fill_color(ctx, icol(ocol, GColorWhite));
    graphics_fill_circle(ctx, GPoint(ox+7, oy+7), 5);
    graphics_context_set_fill_color(ctx, icol(ocol, GColorBlack));
    graphics_fill_circle(ctx, GPoint(ox+9, oy+5), 4);
    graphics_context_set_fill_color(ctx, icol(ocol, GColorWhite));
    px1(ctx, ox+2, oy+2); px1(ctx, ox+13, oy+4); px1(ctx, ox+12, oy+11);
}

/* Dispatch to the matching 16 × 16 pixel-art icon painter.
   icon index: 0=sun, 1=partly cloudy, 2=cloudy, 3=rain,
               4=snow, 5=storm, 6=fog, 7=moon / night
   ocol: GColorClear paints each part's normal colour; any other colour
   forces the whole icon to paint solid in it (used for the outline pass). */
static void draw_weather_icon_ex(GContext *ctx, int ox, int oy, uint8_t icon, GColor ocol) {
    switch (icon) {
        case 0: wx_sun(ctx,ox,oy,ocol);                       break;
        case 1: wx_partly_cloudy(ctx,ox,oy,ocol);             break;
        case 2: wx_cloud_shape(ctx,ox,oy,GColorWhite,ocol);   break;
        case 3: wx_rain(ctx,ox,oy,ocol);                      break;
        case 4: wx_snow(ctx,ox,oy,ocol);                      break;
        case 5: wx_storm(ctx,ox,oy,ocol);                     break;
        case 6: wx_fog(ctx,ox,oy,ocol);                       break;
        case 7: wx_night(ctx,ox,oy,ocol);                     break;
        default: wx_sun(ctx,ox,oy,ocol);                      break;
    }
}

/* Paints a 1 px black outline behind the icon (so pale/white parts stay
   visible on light backgrounds), then the normal-coloured icon on top. */
static void draw_weather_icon(GContext *ctx, int ox, int oy, uint8_t icon) {
    static const int8_t offs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (int i = 0; i < 4; i++) {
        draw_weather_icon_ex(ctx, ox+offs[i][0], oy+offs[i][1], icon, GColorBlack);
    }
    draw_weather_icon_ex(ctx, ox, oy, icon, GColorClear);
}

/* ═══════════════════════════════════════════════════════════
   FOOD ITEM PIXEL-ART  7 x 7  (ox,oy = top-left corner)
   ═══════════════════════════════════════════════════════════ */

/* 16×16 pixel heart  (ox,oy = top-left) – same canvas size as weather icons.
   Drawn at logical 1-px scale, centred in the 16×16 slot. */
static void draw_pixel_heart(GContext *ctx, int ox, int oy, GColor col) {
    /* offset so the 11×10 shape sits centred in 16x16 */
    int dx = ox + 2;
    int dy = oy + 3;
    graphics_context_set_fill_color(ctx, col);
    /* row 0: two bumps, symmetric around the centre notch */
    graphics_fill_rect(ctx, GRect(dx+0, dy+0, 4, 2), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(dx+7, dy+0, 4, 2), 0, GCornerNone);
    /* rows 2-3: full 11 wide */
    graphics_fill_rect(ctx, GRect(dx+0, dy+2, 11, 2), 0, GCornerNone);
    /* row 4: 9 wide */
    graphics_fill_rect(ctx, GRect(dx+1, dy+4, 9, 1), 0, GCornerNone);
    /* row 5: 7 wide */
    graphics_fill_rect(ctx, GRect(dx+2, dy+5, 7, 1), 0, GCornerNone);
    /* row 6: 5 wide */
    graphics_fill_rect(ctx, GRect(dx+3, dy+6, 5, 1), 0, GCornerNone);
    /* row 7: 3 wide */
    graphics_fill_rect(ctx, GRect(dx+4, dy+7, 3, 1), 0, GCornerNone);
    /* row 8: tip */
    graphics_fill_rect(ctx, GRect(dx+5, dy+8, 1, 1), 0, GCornerNone);
}

/* Black outline pass (offset 1 px in each direction) then the coloured
   heart on top, so it stays visible on light backgrounds. */
static void draw_pixel_heart_outlined(GContext *ctx, int ox, int oy, GColor col) {
    static const int8_t offs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (int i = 0; i < 4; i++) {
        draw_pixel_heart(ctx, ox+offs[i][0], oy+offs[i][1], GColorBlack);
    }
    draw_pixel_heart(ctx, ox, oy, col);
}

static void food_apple(GContext *ctx, int ox, int oy, GColor body, GColor leaf) {
    graphics_context_set_fill_color(ctx, leaf);
    px1(ctx, ox+3, oy+0); px1(ctx, ox+4, oy+1);
    graphics_context_set_fill_color(ctx, body);
    graphics_fill_rect(ctx, GRect(ox+1, oy+1, 5, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+0, oy+2, 7, 4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+1, oy+6, 5, 1), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorBlack);
    px1(ctx, ox+3, oy+1);  /* dent */
    graphics_context_set_fill_color(ctx, GColorWhite);
    px1(ctx, ox+1, oy+2); px1(ctx, ox+2, oy+2); px1(ctx, ox+1, oy+3); /* shine */
}

static void food_water_drop(GContext *ctx, int ox, int oy) {
    graphics_context_set_fill_color(ctx, GColorBlueMoon);
    px1(ctx, ox+3, oy+0);
    graphics_fill_rect(ctx, GRect(ox+2, oy+1, 3, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+1, oy+2, 5, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+0, oy+3, 7, 2), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+1, oy+5, 5, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+2, oy+6, 3, 1), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorCeleste);
    px1(ctx, ox+1, oy+3); px1(ctx, ox+2, oy+2);
}

static void food_cherry(GContext *ctx, int ox, int oy) {
    graphics_context_set_fill_color(ctx, GColorIslamicGreen);
    px1(ctx,ox+1,oy+0); px1(ctx,ox+2,oy+1);
    px1(ctx,ox+5,oy+0); px1(ctx,ox+4,oy+1); px1(ctx,ox+3,oy+1);
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, GPoint(ox+1, oy+4), 2);
    graphics_fill_circle(ctx, GPoint(ox+5, oy+4), 2);
    graphics_context_set_fill_color(ctx, GColorFolly);
    px1(ctx, ox+0, oy+3); px1(ctx, ox+4, oy+3);
}

static void food_mushroom(GContext *ctx, int ox, int oy) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, GPoint(ox+3, oy+3), 3);
    graphics_fill_rect(ctx, GRect(ox+0, oy+3, 7, 2), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorWhite);
    px1(ctx,ox+1,oy+1); px1(ctx,ox+4,oy+2); px1(ctx,ox+2,oy+3);
    graphics_fill_rect(ctx, GRect(ox+2, oy+5, 3, 2), 0, GCornerNone);
}

static void food_star(GContext *ctx, int ox, int oy) {
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_rect(ctx, GRect(ox+2, oy+1, 3, 5), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+0, oy+2, 7, 3), 0, GCornerNone);
    px1(ctx,ox+1,oy+1); px1(ctx,ox+5,oy+1);
    px1(ctx,ox+1,oy+5); px1(ctx,ox+5,oy+5);
    graphics_context_set_fill_color(ctx, GColorYellow);
    px1(ctx, ox+3, oy+2); px1(ctx, ox+2, oy+3);
}

static void food_gem(GContext *ctx, int ox, int oy) {
    graphics_context_set_fill_color(ctx, GColorVividViolet);
    graphics_fill_rect(ctx, GRect(ox+2, oy+0, 3, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+1, oy+1, 5, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+0, oy+2, 7, 2), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+1, oy+4, 5, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+2, oy+5, 3, 1), 0, GCornerNone);
    px1(ctx, ox+3, oy+6);
    graphics_context_set_fill_color(ctx, GColorRichBrilliantLavender);
    px1(ctx,ox+1,oy+2); px1(ctx,ox+2,oy+1); px1(ctx,ox+3,oy+2);
}

static void food_coin(GContext *ctx, int ox, int oy) {
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_circle(ctx, GPoint(ox+3, oy+3), 3);
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(ox+3, oy+3), 2);
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    px1(ctx,ox+3,oy+1); px1(ctx,ox+3,oy+5);
    graphics_fill_rect(ctx, GRect(ox+2, oy+2, 2, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(ox+2, oy+4, 2, 1), 0, GCornerNone);
    px1(ctx, ox+4, oy+3);
}

static void food_flower(GContext *ctx, int ox, int oy) {
    /* Yellow centre */
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_circle(ctx, GPoint(ox+3, oy+3), 1);
    /* White petals */
    graphics_context_set_fill_color(ctx, GColorWhite);
    px1(ctx,ox+3,oy+0); px1(ctx,ox+3,oy+6);
    px1(ctx,ox+0,oy+3); px1(ctx,ox+6,oy+3);
    px1(ctx,ox+1,oy+1); px1(ctx,ox+5,oy+1);
    px1(ctx,ox+1,oy+5); px1(ctx,ox+5,oy+5);
    /* Green stem */
    graphics_context_set_fill_color(ctx, GColorIslamicGreen);
    px1(ctx, ox+3, oy+7);
}

/* Paint a 7 × 7 pixel-art food sprite centred on `c`.
   kind: 0=red apple, 1=green apple, 2=yellow apple,
         3=water drop, 4=cherry, 5=mushroom,
         6=star, 7=gem, 8=coin, else=flower */
static void draw_food_item(GContext *ctx, Food *f, GPoint c) {
    int ox = c.x - 3;
    int oy = c.y - 3;
    switch (f->kind) {
        case 0: food_apple(ctx,ox,oy,GColorRed,          GColorIslamicGreen); break;
        case 1: food_apple(ctx,ox,oy,GColorGreen,        GColorDarkGreen);    break;
        case 2: food_apple(ctx,ox,oy,GColorChromeYellow, GColorIslamicGreen); break;
        case 3: food_water_drop(ctx, ox, oy); break;
        case 4: food_cherry(ctx,    ox, oy); break;
        case 5: food_mushroom(ctx,  ox, oy); break;
        case 6: food_star(ctx,      ox, oy); break;
        case 7: food_gem(ctx,       ox, oy); break;
        case 8: food_coin(ctx,      ox, oy); break;
        default: food_flower(ctx,   ox, oy); break;
    }
}

/* ═══════════════════════════════════════════════════════════
   SNAKE RENDERING
   ─────────────────────────────────────────────────────────
   Each body segment: CELL_BODY x CELL_BODY rounded rect.
   Between adjacent segments: a small bridge fills the 2 px gap
   so the body looks fully connected.
   Scale texture: diagonal accent dots on each non-head segment.
   Head: distinct colour, rounder shape, directional eyes + tongue.
   ═══════════════════════════════════════════════════════════ */
static void draw_scale_texture(GContext *ctx, GPoint c, int idx) {
    /* Two-phase alternating scale pattern */
    int phase = idx % 2;
    graphics_context_set_fill_color(ctx, s_cs.s2);
    int bx = c.x - CELL_HALF;
    int by = c.y - CELL_HALF;
    /* Four accent pixels per segment */
    const int8_t off[2][4][2] = {
        {{ 1, 1}, { 5, 1}, { 3, 3}, { 1, 5}},
        {{ 2, 0}, { 6, 2}, { 0, 4}, { 4, 6}},
    };
    for (int i = 0; i < 4; i++) {
        int dx = off[phase][i][0];
        int dy = off[phase][i][1];
        if (dx < CELL_BODY && dy < CELL_BODY) {
            px1(ctx, bx + dx, by + dy);
        }
    }
}

/* Render the snake's head: a rounded rect in shead colour with
   direction-aware pupils (white squares with a black slit centre),
   and a forked tongue protruding in the direction of travel. */

/* Nokia Snake 2 style head – flat 8×8 block with a single 1-px dot eye
   and no tongue, matching the original monochrome pixel game look. */
static void draw_snake_head_nokia2(GContext *ctx) {
    if (s_snake.len == 0) return;
    GPoint c = cell_center(s_snake.body[0]);
    Vec2   d = s_snake.dir;

    /* Flat filled square, no rounding */
    graphics_context_set_fill_color(ctx, s_cs.shead);
    graphics_fill_rect(ctx, GRect(c.x-CELL_HALF, c.y-CELL_HALF,
                                  CELL_BODY, CELL_BODY), 0, GCornerNone);

    /* Single black dot eye, offset in the direction of travel */
    graphics_context_set_fill_color(ctx, s_cs.seye);
    if      (d.x ==  1) { px1(ctx, c.x+2, c.y-1); px1(ctx, c.x+2, c.y+1); }
    else if (d.x == -1) { px1(ctx, c.x-3, c.y-1); px1(ctx, c.x-3, c.y+1); }
    else if (d.y == -1) { px1(ctx, c.x-1, c.y-3); px1(ctx, c.x+1, c.y-3); }
    else                { px1(ctx, c.x-1, c.y+2); px1(ctx, c.x+1, c.y+2); }
}

/* Nokia 3310 authentic head – flat 8×8 square base + 2×2 nose protrusion
   pointing in the direction of travel, matching the characteristic Nokia
   Snake 2 silhouette.  Two 1-px eye marks flank the nose. */
static void draw_snake_head_nokia3310(GContext *ctx) {
    if (s_snake.len == 0) return;
    GPoint c = cell_center(s_snake.body[0]);
    Vec2   d = s_snake.dir;

    graphics_context_set_fill_color(ctx, s_cs.shead);
    /* Base square */
    graphics_fill_rect(ctx, GRect(c.x-CELL_HALF, c.y-CELL_HALF,
                                  CELL_BODY, CELL_BODY), 0, GCornerNone);
    /* Forward nose – 2 wide, 2 deep, centred on the leading edge */
    if      (d.x ==  1) graphics_fill_rect(ctx, GRect(c.x+CELL_HALF, c.y-1, 2, 2), 0, GCornerNone);
    else if (d.x == -1) graphics_fill_rect(ctx, GRect(c.x-CELL_HALF-2, c.y-1, 2, 2), 0, GCornerNone);
    else if (d.y == -1) graphics_fill_rect(ctx, GRect(c.x-1, c.y-CELL_HALF-2, 2, 2), 0, GCornerNone);
    else                graphics_fill_rect(ctx, GRect(c.x-1, c.y+CELL_HALF,   2, 2), 0, GCornerNone);

    /* Eye dots – 1 px each, flanking the nose */
    graphics_context_set_fill_color(ctx, s_cs.seye);
    if      (d.x ==  1) { px1(ctx, c.x+2, c.y-3); px1(ctx, c.x+2, c.y+2); }
    else if (d.x == -1) { px1(ctx, c.x-3, c.y-3); px1(ctx, c.x-3, c.y+2); }
    else if (d.y == -1) { px1(ctx, c.x-3, c.y-3); px1(ctx, c.x+2, c.y-3); }
    else                { px1(ctx, c.x-3, c.y+2); px1(ctx, c.x+2, c.y+2); }
}

static void draw_snake_head(GContext *ctx) {
    /* Nokia Snake 2 skin uses its own blocky head renderer */
    if (s_cfg.snake_skin == 4) { draw_snake_head_nokia2(ctx); return; }
    /* Nokia 3310 authentic skin */
    if (s_cfg.snake_skin == 6) { draw_snake_head_nokia3310(ctx); return; }
    if (s_snake.len == 0) return;
    GPoint c = cell_center(s_snake.body[0]);
    Vec2   d = s_snake.dir;

    /* Head block */
    graphics_context_set_fill_color(ctx, s_cs.shead);
    graphics_fill_rect(ctx, GRect(c.x-CELL_HALF, c.y-CELL_HALF,
                                  CELL_BODY, CELL_BODY), 3, GCornersAll);

    /* Pupils */
    graphics_context_set_fill_color(ctx, s_cs.seye);
    if (d.x == 1) {
        graphics_fill_rect(ctx, GRect(c.x+1, c.y-3, 2, 2), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(c.x+1, c.y+1, 2, 2), 0, GCornerNone);
    } else if (d.x == -1) {
        graphics_fill_rect(ctx, GRect(c.x-3, c.y-3, 2, 2), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(c.x-3, c.y+1, 2, 2), 0, GCornerNone);
    } else if (d.y == -1) {
        graphics_fill_rect(ctx, GRect(c.x-3, c.y-3, 2, 2), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(c.x+1, c.y-3, 2, 2), 0, GCornerNone);
    } else {
        graphics_fill_rect(ctx, GRect(c.x-3, c.y+1, 2, 2), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(c.x+1, c.y+1, 2, 2), 0, GCornerNone);
    }

    /* Slit pupils (dark centre dot) */
    graphics_context_set_fill_color(ctx, GColorBlack);
    if      (d.x ==  1) { px1(ctx,c.x+2,c.y-2); px1(ctx,c.x+2,c.y+2); }
    else if (d.x == -1) { px1(ctx,c.x-2,c.y-2); px1(ctx,c.x-2,c.y+2); }
    else if (d.y == -1) { px1(ctx,c.x-2,c.y-2); px1(ctx,c.x+2,c.y-2); }
    else                { px1(ctx,c.x-2,c.y+2); px1(ctx,c.x+2,c.y+2); }

    /* Forked tongue */
    graphics_context_set_fill_color(ctx, s_cs.stongue);
    if (d.x == 1) {
        px1(ctx,c.x+4,c.y);
        px1(ctx,c.x+5,c.y-1); px1(ctx,c.x+5,c.y+1);
    } else if (d.x == -1) {
        px1(ctx,c.x-4,c.y);
        px1(ctx,c.x-5,c.y-1); px1(ctx,c.x-5,c.y+1);
    } else if (d.y == -1) {
        px1(ctx,c.x,c.y-4);
        px1(ctx,c.x-1,c.y-5); px1(ctx,c.x+1,c.y-5);
    } else {
        px1(ctx,c.x,c.y+4);
        px1(ctx,c.x-1,c.y+5); px1(ctx,c.x+1,c.y+5);
    }
}

/* Render the entire snake in three ordered passes:
 *  1. Connector bridges – narrow fills between adjacent segment centres
 *     so the body appears as a continuous tube rather than a chain of
 *     disconnected squares.
 *  2. Rounded body squares with alternating scale-texture accent dots.
 *     The tail tip is drawn 1 px smaller (tapered) for a polished look.
 *  3. The head on top so it always occludes any bridge artefacts. */
static void draw_snake(GContext *ctx) {
    if (s_snake.len == 0) return;
    bool nokia2  = (s_cfg.snake_skin == 4); /* Nokia Snake 2: flat blocks, checkerboard */
    bool nokia6  = (s_cfg.snake_skin == 6); /* Nokia 3310 authentic: clean flat blocks */
    bool nokia   = nokia2 || nokia6;        /* any Nokia-style flat rendering */

    /* 1. Body colour fills and bridges between adjacent segments */
    graphics_context_set_fill_color(ctx, s_cs.s1);
    for (int i = 0; i < s_snake.len - 1; i++) {
        GPoint ca = cell_center(s_snake.body[i]);
        GPoint cb = cell_center(s_snake.body[i+1]);
        int dx = cb.x - ca.x;
        int dy = cb.y - ca.y;

        if (dx != 0) {
            /* Horizontal bridge: fill the 2 px gap between cells */
            int bx = (dx > 0) ? ca.x + CELL_HALF : cb.x + CELL_HALF;
            int bh = nokia ? CELL_BODY : CELL_BODY - 2;
            int by_off = nokia ? 0 : 1;
            graphics_fill_rect(ctx, GRect(bx, ca.y - CELL_HALF + by_off,
                                          GRID_CELL - CELL_BODY,
                                          bh), 0, GCornerNone);
        } else {
            /* Vertical bridge */
            int by = (dy > 0) ? ca.y + CELL_HALF : cb.y + CELL_HALF;
            int bw = nokia ? CELL_BODY : CELL_BODY - 2;
            int bx_off = nokia ? 0 : 1;
            graphics_fill_rect(ctx, GRect(ca.x - CELL_HALF + bx_off, by,
                                          bw,
                                          GRID_CELL - CELL_BODY), 0, GCornerNone);
        }
    }

    /* 2. Segment squares (skip index 0 = head, drawn last) */
    for (int i = 1; i < s_snake.len; i++) {
        GPoint c = cell_center(s_snake.body[i]);
        /* Nokia Snake 2: flat full-size squares, no taper, no texture */
        if (nokia2) {
            graphics_context_set_fill_color(ctx, s_cs.s1);
            graphics_fill_rect(ctx, GRect(c.x-CELL_HALF, c.y-CELL_HALF,
                                          CELL_BODY, CELL_BODY), 0, GCornerNone);
            /* Darker inner pixel for checkerboard Nokia body feel */
            if (i % 2 == 0) {
                graphics_context_set_fill_color(ctx, s_cs.s2);
                graphics_fill_rect(ctx, GRect(c.x-2, c.y-2, 4, 4), 0, GCornerNone);
            }
            continue;
        }
        /* Nokia 3310 authentic: clean flat squares, no taper, no texture */
        if (nokia6) {
            graphics_context_set_fill_color(ctx, s_cs.s1);
            graphics_fill_rect(ctx, GRect(c.x-CELL_HALF, c.y-CELL_HALF,
                                          CELL_BODY, CELL_BODY), 0, GCornerNone);
            continue;
        }
        /* Tail tip tapers */
        int half = (i == s_snake.len - 1) ? CELL_HALF - 1 : CELL_HALF;
        int sz   = half * 2;
        int rad  = (i == s_snake.len - 1) ? 4 : 2;

        graphics_context_set_fill_color(ctx, s_cs.s1);
        graphics_fill_rect(ctx, GRect(c.x-half, c.y-half, sz, sz), rad, GCornersAll);

        /* Scale texture */
        if (i < s_snake.len - 1) {
            draw_scale_texture(ctx, c, i);
        }
    }

    /* 3. Head (drawn on top) */
    draw_snake_head(ctx);
}

/* ═══════════════════════════════════════════════════════════
   INFO BAR  (top INFO_H px)
   ═══════════════════════════════════════════════════════════ */

/* Render the top status bar.
   Layout (symmetric):
     Left:  [icon_gap][weather 16px][2px][temp box]
     Right: [batt box][2px][heart 16px][icon_gap]
     Centre: date box equidistant between temp and batt box.
   When show_weather is false the left anchor is icon_gap (no icon/temp). */
static void draw_info_bar(GContext *ctx) {
    bool wide   = (s_sw >= 180);
    int  icon_gap = wide ? 4 : 2;
    int  icon_w   = 16;
    int  icon_y   = (INFO_H - icon_w) / 2;
    int  box_h    = 18;
    int  box_y    = (INFO_H - box_h) / 2;
    int  tb_w     = wide ? 46 : 36;   /* temp box width */
    int  bb_w     = tb_w;             /* batt box = same width */

    /* ── RIGHT SIDE (always visible) ── */
    /* Heart icon flush to right edge */
    int heart_x = s_sw - icon_w - icon_gap;
    draw_pixel_heart_outlined(ctx, heart_x, icon_y, GColorRed);

    /* Battery box, 2 px gap left of heart */
    int bb_x = heart_x - 2 - bb_w;
    char bat[8];
    snprintf(bat, sizeof(bat), "%d%%", s_batt.charge_percent);
    draw_box(ctx, GRect(bb_x, box_y, bb_w, box_h),
             s_cs.box_bg, s_cs.box_fg, bat, s_fnt_small);

    /* ── LEFT SIDE ── */
    int left_content_end;  /* x right-edge of left content (after temp or icon_gap) */

    if (s_cfg.show_weather) {
        /* Weather icon */
        draw_weather_icon(ctx, icon_gap, icon_y, s_wx_valid ? s_wx : 0);

        /* Temperature box */
        char tmp[14];
        if (s_wx_valid) {
            snprintf(tmp, sizeof(tmp), "%d\xc2\xb0%c",
                     (int)s_temp, s_cfg.celsius ? 'C' : 'F');
        } else {
            snprintf(tmp, sizeof(tmp), "--\xc2\xb0" "C");
        }
        int tb_x = icon_gap + icon_w + 2;
        draw_box(ctx, GRect(tb_x, box_y, tb_w, box_h),
                 s_cs.box_bg, s_cs.box_fg, tmp, s_fnt_small);
        left_content_end = tb_x + tb_w;
    } else {
        left_content_end = icon_gap;
    }

    /* ── DATE BOX – centred between left content and battery box ── */
    char dat[32];
    snprintf(dat, sizeof(dat), "%02d.%02d.%02d",
             (int)s_tm.tm_mday, (int)(s_tm.tm_mon + 1),
             (int)((unsigned)s_tm.tm_year % 100));
    int avail  = bb_x - left_content_end;
    int db_w   = wide ? 66 : 52;
    if (db_w > avail - 4) db_w = avail - 4;  /* clamp if screen too narrow */
    int db_x   = left_content_end + (avail - db_w) / 2;
    draw_box(ctx, GRect(db_x, box_y, db_w, box_h),
             s_cs.box_bg, s_cs.box_fg, dat, s_fnt_small);
}

/* ═══════════════════════════════════════════════════════════
   PIXEL CLOCK RENDERING  –  hand-drawn 3×5 digits, slide animation
   ═══════════════════════════════════════════════════════════ */

/* Render a single 3×5 pixel digit at grid top-left (ox, oy).
   scale   : screen pixels per bitmap pixel.
   col     : colour for active (lit) pixels.
   off_col : colour for inactive pixels (used in LCD mode only).
   mode    : 0=solid squares  1=snake-rounded  2=LCD (also draws dim inactive)
             3=scanlines (1-px gap per bitmap row for a CRT feel).
   y_off   : vertical shift for slide animation (positive = shifted down / in from below).
   Clips to [clip_top, clip_bot) so the reveal wipe looks clean. */
static void draw_pxdigit(GContext *ctx, int d, int ox, int oy, int scale,
                          GColor col, GColor off_col, int mode,
                          int y_off, int clip_top, int clip_bot) {
    if (d < 0 || d > 9) return;
    const uint8_t *bmp = PXDIGIT[d];
    int r      = (mode == 1) ? (scale > 5 ? 3 : 1) : 0;
    int psz    = (mode == 1) ? scale - 1 : scale;  /* pixel block size      */
    int scan_h = (mode == 3) ? scale - 1 : psz;    /* scanlines: 1-px gap   */
    for (int row = 0; row < 5; row++) {
        int py = oy + y_off + row * scale;
        if (py + scan_h <= clip_top || py >= clip_bot) continue;
        int ry0 = (py < clip_top)            ? clip_top - py     : 0;
        int ry1 = (py + scan_h > clip_bot)   ? clip_bot - py     : scan_h;
        if (ry1 <= ry0) continue;
        for (int c = 0; c < 3; c++) {
            bool on = (bmp[row] >> (2 - c)) & 1;
            if (!on && mode != 2) continue; /* skip inactive unless LCD mode */
            graphics_context_set_fill_color(ctx, on ? col : off_col);
            int px_x = ox + c * scale;
            graphics_fill_rect(ctx, GRect(px_x, py + ry0, psz, ry1 - ry0),
                               (ry0 == 0 && ry1 == scan_h) ? r : 0, GCornersAll);
        }
    }
}

/* Draw the colon ':' as two pixel dots at bitmap rows 1 and 3. */
static void draw_pxcolon(GContext *ctx, int ox, int oy, int scale,
                          GColor col, int mode,
                          int y_off, int clip_top, int clip_bot) {
    int psz    = (mode == 1) ? scale - 1 : scale;
    int scan_h = (mode == 3) ? scale - 1 : psz;
    int r      = (mode == 1) ? (scale > 5 ? 3 : 1) : 0;
    const int dot_rows[2] = {1, 3};
    graphics_context_set_fill_color(ctx, col);
    for (int i = 0; i < 2; i++) {
        int py = oy + y_off + dot_rows[i] * scale;
        if (py + scan_h <= clip_top || py >= clip_bot) continue;
        int ry0 = (py < clip_top)          ? clip_top - py : 0;
        int ry1 = (py + scan_h > clip_bot) ? clip_bot - py : scan_h;
        if (ry1 <= ry0) continue;
        graphics_fill_rect(ctx, GRect(ox, py + ry0, psz, ry1 - ry0),
                           (ry0 == 0 && ry1 == scan_h) ? r : 0, GCornersAll);
    }
}

/* Render the full HH:MM pixel-art clock, vertically centred at s_clock_cy.
   Called by draw_clock() when clock_font >= 5.  mode = clock_font - 5:
     0 = Big Block   – bold solid squares, almost full screen width
     1 = Snake Pixel – rounded segments (same visual language as the snake body)
     2 = LCD Matrix  – lit pixels bright; inactive pixels shown in dim grey
     3 = Scanlines   – 1-px gap per bitmap row, retro CRT monitor feel
   All modes use a slide-up animation when a digit changes. */
static void draw_pixel_clock(GContext *ctx, int mode) {
    int scale    = px_clock_scale();
    GColor col     = s_cs.clock_fg;
    GColor off_col = (mode == 2) ? GColorDarkGray : col;

    /* Layout: 4 digit-glyphs (3 cols each) + 1 colon (1 col) + 4 gaps (1 col each)
       Total width = 17 * scale */
    int total_w  = 17 * scale;
    int ox0      = (s_sw - total_w) / 2;
    int digit_h  = 5 * scale;
    int oy       = s_clock_cy - digit_h / 2;
    int clip_top = oy;
    int clip_bot = oy + digit_h;

    /* Halo behind the clock so digits remain legible over the game field */
    GRect halo = GRect(4, clip_top - 4, s_sw - 8, digit_h + 8);
    graphics_context_set_fill_color(ctx, s_cs.bg);
    graphics_fill_rect(ctx, halo, 8, GCornersAll);

    /* Current time, respecting 12 h / 24 h setting */
    int h24 = s_tm.tm_hour;
    int h   = s_cfg.show_24h ? h24 : (h24 % 12 ? h24 % 12 : 12);
    int m   = s_tm.tm_min;
    int cur[4] = { h / 10, h % 10, m / 10, m % 10 };

    /* X positions for the five character slots */
    int dw  = 3 * scale;   /* digit width  */
    int gap = scale;        /* inter-char gap */
    int cw  = scale;        /* colon width  */
    int xp[5];
    xp[0] = ox0;
    xp[1] = xp[0] + dw + gap;
    xp[2] = xp[1] + dw + gap;   /* colon */
    xp[3] = xp[2] + cw + gap;
    xp[4] = xp[3] + dw + gap;

    /* digit-index (0-3: H1,H2,M1,M2) → xp-slot index (0,1,3,4) */
    const int dxp[4] = {0, 1, 3, 4};

    /* How many pixels the outgoing digit has slid upward so far */
    int shift = 0;
    if (s_clock_anim_frame > 0) {
        int prog = CLOCK_ANIM_FRAMES - s_clock_anim_frame;
        shift = (digit_h * prog + CLOCK_ANIM_FRAMES / 2) / CLOCK_ANIM_FRAMES;
    }

    /* Colon (never animated) */
    draw_pxcolon(ctx, xp[2], oy, scale, col, mode, 0, clip_top, clip_bot);

    /* Digits – animate only the ones that actually changed */
    for (int i = 0; i < 4; i++) {
        int xd = xp[dxp[i]];
        if (s_clock_anim_frame > 0 && (s_clock_anim_mask & (1 << i))) {
            /* Outgoing digit slides upward and out */
            draw_pxdigit(ctx, s_clock_anim_old[i], xd, oy, scale,
                         col, off_col, mode, -shift, clip_top, clip_bot);
            /* Incoming digit slides up from below */
            draw_pxdigit(ctx, cur[i], xd, oy, scale,
                         col, off_col, mode, digit_h - shift, clip_top, clip_bot);
        } else {
            draw_pxdigit(ctx, cur[i], xd, oy, scale,
                         col, off_col, mode, 0, clip_top, clip_bot);
        }
    }

    /* AM/PM label for 12 h mode */
    if (!s_cfg.show_24h) {
        const char *ap = (h24 < 12) ? "AM" : "PM";
        GRect apr = GRect(xp[4] + dw + 3, oy + digit_h - 10, 24, 12);
        graphics_context_set_text_color(ctx, col);
        graphics_draw_text(ctx, ap, s_fnt_small, apr,
                           GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
}

/* ═══════════════════════════════════════════════════════════
   CLOCK  (centred in the play zone)
   ═══════════════════════════════════════════════════════════ */

/* Render the clock at s_clock_cy (vertical centre of the play zone).
   A dark rounded-rect halo is painted first so digits remain legible
   regardless of where the snake is sitting.
   Supports 24 h mode (HH:MM) and 12 h mode (H:MM with AM/PM label). */
static void draw_clock(GContext *ctx) {
    /* Dispatch to pixel art renderer for clock styles 5–8 */
    if (s_cfg.clock_font >= 5) {
        draw_pixel_clock(ctx, (int)s_cfg.clock_font - 5);
        return;
    }
    char t[10];
    if (s_cfg.show_24h) {
        snprintf(t, sizeof(t), "%02d:%02d",
                 s_tm.tm_hour, s_tm.tm_min);
    } else {
        int h = s_tm.tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(t, sizeof(t), "%2d:%02d", h, s_tm.tm_min);
    }

    int cy = s_clock_cy;
    /* Halo behind the clock so digits are always legible */
    GRect halo = GRect(6, cy - s_clock_half - 4, s_sw - 12, (s_clock_half + 4) * 2);
    graphics_context_set_fill_color(ctx, s_cs.bg);
    graphics_fill_rect(ctx, halo, 8, GCornersAll);

    GRect tr = GRect(0, cy - s_clock_half, s_sw, s_clock_half * 2);
    shadowed_text(ctx, t, s_fnt_clock, tr, s_cs.clock_fg, GTextAlignmentCenter);

    /* AM/PM label for 12 h mode */
    if (!s_cfg.show_24h) {
        const char *ap = (s_tm.tm_hour < 12) ? "AM" : "PM";
        GRect apr = GRect(s_sw/2 + 56, cy - 8, 22, 14);
        graphics_context_set_text_color(ctx, s_cs.clock_fg);
        graphics_draw_text(ctx, ap, s_fnt_small, apr,
                           GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
}

/* ═══════════════════════════════════════════════════════════
   WEEKDAY BAR  (bottom WEEK_H px)
   ═══════════════════════════════════════════════════════════ */
static const char *WD_DE[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
static const char *WD_EN[] = {"Mo","Tu","We","Th","Fr","Sa","Su"};
static const char *WD_FR[] = {"Lu","Ma","Me","Je","Ve","Sa","Di"};
static const char *WD_ES[] = {"Lu","Ma","Mi","Ju","Vi","Sa","Do"};
static const char *WD_IT[] = {"Lu","Ma","Me","Gi","Ve","Sa","Do"};

/* Return the two-letter weekday abbreviation for bar index `i` (0=Mon…6=Sun)
   in the language selected by s_cfg.language (0=DE, 1=EN, 2=FR, 3=ES, 4=IT). */
static const char *weekday_name(int i) {
    const char **wd;
    switch (s_cfg.language) {
        case 1: wd = WD_EN; break;
        case 2: wd = WD_FR; break;
        case 3: wd = WD_ES; break;
        case 4: wd = WD_IT; break;
        default: wd = WD_DE; break;
    }
    return wd[i];
}

/* Render the bottom weekday strip.
   Today's cell is drawn as a filled rounded box (day_bg / day_fg);
   all other days use plain text in day_off colour. */
static void draw_weekday_bar(GContext *ctx) {
    int today  = today_idx();
    int item_w = s_sw / 7;
    int bar_y  = s_sh - WEEK_H;
    int box_h  = 18;
    int box_y  = bar_y + (WEEK_H - box_h) / 2;

    for (int i = 0; i < 7; i++) {
        int bx = i * item_w + 1;
        int bw = item_w - 2;
        if (i == today) {
            draw_box(ctx, GRect(bx, box_y, bw, box_h),
                     s_cs.day_bg, s_cs.day_fg,
                     weekday_name(i), s_fnt_small);
        } else {
            graphics_context_set_text_color(ctx, s_cs.day_off);
            GRect tr = GRect(bx, box_y + 1, bw, box_h - 2);
            graphics_draw_text(ctx, weekday_name(i), s_fnt_small, tr,
                               GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        }
    }
}

/* ═══════════════════════════════════════════════════════════
   MASTER CANVAS UPDATE PROC
   ═══════════════════════════════════════════════════════════ */

/* Layer update_proc: redraws the entire framebuffer in back-to-front order.
   Draw order:
     1. Background fill
     2. Food item sprites
     3. Snake body + head
     4. Info bar overlay (top)
     5. Clock halo + digits (centre)
     6. Weekday bar overlay (bottom) */
static void canvas_update(Layer *layer, GContext *ctx) {
    /* Background */
    graphics_context_set_fill_color(ctx, s_cs.bg);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    /* Food items (behind snake) */
    for (int f = 0; f < FOOD_MAX; f++) {
        if (s_food[f].active) {
            draw_food_item(ctx, &s_food[f], cell_center(s_food[f].pos));
        }
    }

    /* Snake */
    draw_snake(ctx);

    /* UI on top */
    draw_info_bar(ctx);
    draw_clock(ctx);
    draw_weekday_bar(ctx);
}

/* Fired 400 ms after the last settings key arrived.
   Triggers a final redraw to ensure any font or layout change that
   arrived near a frame boundary is reflected on screen. */
static void settings_apply_callback(void *data) {
    s_settings_apply_timer = NULL;
    if (s_canvas) layer_mark_dirty(s_canvas);
}

/* ═══════════════════════════════════════════════════════════
   SNAKE SPAWN / STEP  (one step per minute, respawn each full hour)
   ═══════════════════════════════════════════════════════════ */

/* Spawn a fresh snake and reset food.  Called once on watchface appear
   and again at the start of each new hour. */
static void reset_snake(void) {
    spawn_snake();
    init_food();
}

/* Step animation callback – advances one step, re-arms if more remain. */
static void step_anim_callback(void *data) {
    s_step_anim_timer = NULL;
    if (s_steps_remaining == 0) return;
    snake_step();
    s_steps_remaining--;
    layer_mark_dirty(s_canvas);
    if (s_steps_remaining > 0) {
        uint8_t spd = s_cfg.snake_speed < 5 ? s_cfg.snake_speed : 2;
        s_step_anim_timer = app_timer_register(SPEED_MS[spd], step_anim_callback, NULL);
    }
}

/* Clock digit slide animation callback – fires CLOCK_ANIM_MS after each frame. */
static void clock_anim_callback(void *data) {
    s_clock_anim_timer = NULL;
    if (s_clock_anim_frame > 0) {
        s_clock_anim_frame--;
        if (s_clock_anim_frame > 0) {
            s_clock_anim_timer = app_timer_register(CLOCK_ANIM_MS,
                                                    clock_anim_callback, NULL);
        }
    }
    if (s_canvas) layer_mark_dirty(s_canvas);
}

/* Start a digit-slide animation for pixel clock styles (clock_font >= 5).
   Compares old vs new time to find which digit positions changed and stores
   them so draw_pixel_clock() can render both digits during the transition. */
static void start_clock_anim(int old_h24, int old_m, int new_h24, int new_m) {
    if (s_cfg.clock_font < 5) return;
    if (s_clock_anim_timer) {
        app_timer_cancel(s_clock_anim_timer);
        s_clock_anim_timer = NULL;
    }
    /* Convert to the same display format as draw_pixel_clock() uses */
    int old_h = s_cfg.show_24h ? old_h24 : (old_h24 % 12 ? old_h24 % 12 : 12);
    int new_h = s_cfg.show_24h ? new_h24 : (new_h24 % 12 ? new_h24 % 12 : 12);

    s_clock_anim_old[0] = (uint8_t)(old_h / 10);
    s_clock_anim_old[1] = (uint8_t)(old_h % 10);
    s_clock_anim_old[2] = (uint8_t)(old_m / 10);
    s_clock_anim_old[3] = (uint8_t)(old_m % 10);

    s_clock_anim_mask = 0;
    if (old_h / 10 != new_h / 10) s_clock_anim_mask |= 0x01;
    if (old_h % 10 != new_h % 10) s_clock_anim_mask |= 0x02;
    if (old_m / 10 != new_m / 10) s_clock_anim_mask |= 0x04;
    if (old_m % 10 != new_m % 10) s_clock_anim_mask |= 0x08;
    if (s_clock_anim_mask == 0) return;

    s_clock_anim_frame = CLOCK_ANIM_FRAMES;
    s_clock_anim_timer = app_timer_register(CLOCK_ANIM_MS, clock_anim_callback, NULL);
}

/* TickTimerService callback – subscribed at MINUTE_UNIT.
 * Per Pebble best practice, MINUTE_UNIT saves ~59 CPU wake-ups per minute
 * vs SECOND_UNIT.  The snake advances exactly one step per minute tick,
 * making it a slow living clock hand.  At the top of each hour the snake
 * is respawned so it always starts fresh for the new hour. */
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    int prev_h = s_tm.tm_hour;
    int prev_m = s_tm.tm_min;
    s_tm = *tick_time;
    /* Rebuild colour scheme every minute so Auto mode updates at hour boundaries */
    build_scheme();
    /* Pixel clock digit slide animation */
    start_clock_anim(prev_h, prev_m, s_tm.tm_hour, s_tm.tm_min);
    /* At the full hour: respawn the snake fresh for the new hour */
    if (tick_time->tm_min == 0) {
        /* Cancel any ongoing step animation before respawn */
        if (s_step_anim_timer) { app_timer_cancel(s_step_anim_timer); s_step_anim_timer = NULL; }
        s_steps_remaining = 0;
        reset_snake();
    }
    /* Animate N steps spread over SPEED_MS intervals */
    static const uint8_t STEPS_TABLE[5] = {1, 2, 3, 5, 8};
    uint8_t idx = s_cfg.steps_per_min < 5 ? s_cfg.steps_per_min : 0;
    s_steps_remaining = STEPS_TABLE[idx];
    /* Kick off the first step immediately */
    step_anim_callback(NULL);
    /* layer_mark_dirty happens inside step_anim_callback */
}

/* BatteryStateService callback: cache the new state and schedule a redraw.
   Battery changes are rare events so the redraw cost is negligible. */
static void battery_callback(BatteryChargeState state) {
    s_batt = state;
    if (s_canvas) layer_mark_dirty(s_canvas);
}

/* ═══════════════════════════════════════════════════════════
   APPMESSAGE  (settings + weather from phone)
   ═══════════════════════════════════════════════════════════ */

/* AppMessage inbox callback – handles both weather data and Clay settings.
 *
 * Each key is optional; only present keys update s_cfg and flash storage.
 * After all keys are processed, build_scheme() and apply_fonts() apply the
 * changes immediately so the display updates without waiting for the next
 * full redraw cycle.
 *
 * A 400 ms debounce timer (s_settings_apply_timer) is reset on every call
 * so that when a batch of keys arrives in quick succession only a single
 * start_animation() is triggered once the stream goes quiet. */
static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *t;

    t = dict_find(iter, KEY_TEMPERATURE);
    if (t) {
        s_temp     = (int8_t)t->value->int32;
        s_wx_valid = true;
        persist_write_int(PK_TEMPERATURE, s_temp);
    }
    t = dict_find(iter, KEY_WEATHER_ICON);
    if (t) {
        s_wx = (uint8_t)t->value->int32;
        persist_write_int(PK_WEATHER_ICON, s_wx);
    }
    t = dict_find(iter, KEY_COLOR_SCHEME);
    if (t) {
        s_cfg.color_scheme = (uint8_t)t->value->int32;
        persist_write_int(PK_COLOR_SCHEME, s_cfg.color_scheme);
    }
    t = dict_find(iter, KEY_LANGUAGE);
    if (t) {
        s_cfg.language = (uint8_t)t->value->int32;
        persist_write_int(PK_LANGUAGE, s_cfg.language);
    }
    t = dict_find(iter, KEY_SNAKE_SKIN);
    if (t) {
        s_cfg.snake_skin = (uint8_t)t->value->int32;
        persist_write_int(PK_SNAKE_SKIN, s_cfg.snake_skin);
    }
    t = dict_find(iter, KEY_SNAKE_SPEED);
    if (t) {
        s_cfg.snake_speed = (uint8_t)t->value->int32;
        persist_write_int(PK_SNAKE_SPEED, s_cfg.snake_speed);
    }
    t = dict_find(iter, KEY_SNAKE_GROWTH);
    if (t) {
        s_cfg.snake_growth = (uint8_t)t->value->int32;
        persist_write_int(PK_SNAKE_GROWTH, s_cfg.snake_growth);
    }
    t = dict_find(iter, KEY_STEPS_PER_MIN);
    if (t) {
        s_cfg.steps_per_min = (uint8_t)t->value->int32;
        persist_write_int(PK_STEPS_PER_MIN, s_cfg.steps_per_min);
    }
    t = dict_find(iter, KEY_ITEM_SET);
    if (t) {
        s_cfg.item_set = (uint8_t)t->value->int32;
        persist_write_int(PK_ITEM_SET, s_cfg.item_set);
    }
    t = dict_find(iter, KEY_CELSIUS);
    if (t) {
        s_cfg.celsius = (bool)t->value->int32;
        persist_write_int(PK_CELSIUS, s_cfg.celsius ? 1 : 0);
    }
    t = dict_find(iter, KEY_SHOW_24H);
    if (t) {
        s_cfg.show_24h = (bool)t->value->int32;
        persist_write_int(PK_SHOW_24H, s_cfg.show_24h ? 1 : 0);
    }
    t = dict_find(iter, KEY_SHOW_WEATHER);
    if (t) {
        s_cfg.show_weather = (bool)t->value->int32;
        persist_write_int(PK_SHOW_WEATHER, s_cfg.show_weather ? 1 : 0);
    }
    t = dict_find(iter, KEY_ANIM_EVERY_MINUTE);
    if (t) {
        s_cfg.anim_every_minute = (bool)t->value->int32;
        persist_write_int(PK_ANIM_EVERY_MINUTE, s_cfg.anim_every_minute ? 1 : 0);
    }
    t = dict_find(iter, KEY_CLOCK_FONT);
    if (t) {
        s_cfg.clock_font = (uint8_t)t->value->int32;
        persist_write_int(PK_CLOCK_FONT, s_cfg.clock_font);
    }

    build_scheme();
    apply_fonts();
    /* Restore Bluetooth low-power sniff interval immediately after settings
       arrive.  AppMessage temporarily raises the BT activity level; calling
       this returns to normal sniff mode and saves power (Rebble battery guide). */
    app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL);
    if (s_canvas) layer_mark_dirty(s_canvas);
    /* Debounce: wait for the full settings batch before doing anything else */
    if (s_settings_apply_timer) app_timer_cancel(s_settings_apply_timer);
    s_settings_apply_timer = app_timer_register(400, settings_apply_callback, NULL);
}

/* ═══════════════════════════════════════════════════════════
   WINDOW HANDLERS
   ═══════════════════════════════════════════════════════════ */

/* Measure the screen, compute layout constants, create the canvas layer,
   seed the PRNG, and load the initial colour scheme + fonts.
   The snake is spawned here (once) so returning from notifications never
   resets the game state – window_appear only triggers a redraw. */
static void window_load(Window *window) {
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    s_sw = bounds.size.w;
    s_sh = bounds.size.h;
    s_gw = s_sw / GRID_CELL;
    s_gh = s_sh / GRID_CELL;

    /* Clock centre: halfway between info-bar and weekday-bar */
    s_clock_cy = INFO_H + (s_sh - INFO_H - WEEK_H) / 2;

    /* Play zone row bounds (snake + food must stay inside) */
    s_gy_min = (INFO_H + GRID_CELL - 1) / GRID_CELL;
    s_gy_max = (s_sh - WEEK_H) / GRID_CELL - 1;

    s_fnt_small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    apply_fonts();

    s_canvas = layer_create(bounds);
    layer_set_update_proc(s_canvas, canvas_update);
    layer_add_child(root, s_canvas);

    s_batt = battery_state_service_peek();

    time_t   now = time(NULL);
    struct tm *lt = localtime(&now);
    s_tm = *lt;

    build_scheme();

    /* State is restored from flash in init(); only spawn fresh here on the
       very first run (no saved state) or if the flag wasn't set by init(). */
    if (!s_game_initialized) {
        s_rand = (uint32_t)time(NULL) ^ 0xDEADBEEFu;
        reset_snake();
        s_game_initialized = true;
    }
}

static void window_appear(Window *window) {
    /* Just refresh the display – state is preserved from wherever we left off.
       The initial spawn happens in window_load (fires only once at app start),
       so returning from notifications or other windows never resets the snake. */
    layer_mark_dirty(s_canvas);
}

/* Cancel all pending timers and destroy the canvas layer before the
   window is freed.  Mirrors every timer/layer created in window_load. */
static void window_unload(Window *window) {
    if (s_step_anim_timer) {
        app_timer_cancel(s_step_anim_timer);
        s_step_anim_timer = NULL;
    }
    if (s_clock_anim_timer) {
        app_timer_cancel(s_clock_anim_timer);
        s_clock_anim_timer = NULL;
    }
    if (s_settings_apply_timer) {
        app_timer_cancel(s_settings_apply_timer);
        s_settings_apply_timer = NULL;
    }
    /* Release custom clock font before the window is freed */
    if (s_fnt_clock_custom) {
        fonts_unload_custom_font(s_fnt_clock_custom);
        s_fnt_clock_custom = NULL;
    }
    layer_destroy(s_canvas);
}

/* ═══════════════════════════════════════════════════════════
   APP INIT / DEINIT
   ═══════════════════════════════════════════════════════════ */

/* One-time application initialisation.
   Restores persisted settings, registers all system-service callbacks,
   creates the window, and pushes it onto the stack – which fires
   window_load followed immediately by window_appear, starting the first
   snake animation. */
static void init(void) {
    load_config();

    /* Restore game state saved by the previous run.  Must happen before
       window_load so the snake is already valid when the canvas first draws. */
    if (load_game_state()) {
        s_game_initialized = true;  /* suppress fresh spawn in window_load */
    }

    app_message_register_inbox_received(inbox_received);
    /* 256 bytes inbox is ample for ~15 small integer settings key-value pairs.
       Reduced from 1024 to lower peak RAM pressure during message processing. */
    app_message_open(256, 64);

    /* Subscribe at MINUTE_UNIT only – the watchface shows HH:MM (no seconds),
       and the snake animation drives its own AppTimer for per-frame redraws.
       SECOND_UNIT would wake the CPU 60× per minute for no benefit
       (Rebble best practice: Conserving Battery Life). */
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);

    s_win = window_create();
    window_set_background_color(s_win, GColorBlack);
    window_set_window_handlers(s_win, (WindowHandlers){
        .load   = window_load,
        .appear = window_appear,
        .unload = window_unload,
    });
    window_stack_push(s_win, true);
}

/* Unsubscribe all system services and destroy the window on app exit. */
static void deinit(void) {
    save_game_state(); /* persist before the OS kills us */
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    window_destroy(s_win);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
