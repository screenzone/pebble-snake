/**
 * Clay configuration for Snake 2 Watchface
 * Defines all user-configurable settings shown in the Pebble app.
 */
module.exports = [
  {
    "type": "heading",
    "defaultValue": "Snake 2 Watchface"
  },

  /* ─── DISPLAY ─────────────────────────────────────────── */
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Display"
      },
      {
        "type": "select",
        "messageKey": "COLOR_SCHEME",
        "label": "Color Scheme",
        "defaultValue": "0",
        "options": [
          { "label": "Classic (White / Green)",   "value": "0" },
          { "label": "Matrix (Green glow)",        "value": "1" },
          { "label": "Ocean (Blue tones)",          "value": "2" },
          { "label": "Sunset (Orange / Red)",       "value": "3" },
          { "label": "White (bright day)",          "value": "4" },
          { "label": "Auto (changes with time)",    "value": "5" },
          { "label": "White Dark (black boxes)",    "value": "6" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "SHOW_24H",
        "label": "24-hour clock",
        "defaultValue": "1"
      },
      {
        "type": "select",
        "messageKey": "CLOCK_FONT",
        "label": "Clock style",
        "defaultValue": "5",
        "options": [
          { "label": "XS – LECO 36",              "value": "0" },
          { "label": "S  – LECO 42",              "value": "1" },
          { "label": "M  – Roboto 49",            "value": "2" },
          { "label": "L  – FiraSans 48",          "value": "3" },
          { "label": "XL – FiraSans 60",          "value": "4" },
          { "label": "Pixel – Block",             "value": "5" },
          { "label": "Pixel – Snake",             "value": "6" },
          { "label": "Pixel – LCD Matrix",        "value": "7" },
          { "label": "Pixel – Scanlines",         "value": "8" }
        ]
      }
    ]
  },

  /* ─── WEATHER ─────────────────────────────────────────── */
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Weather"
      },
      {
        "type": "toggle",
        "messageKey": "SHOW_WEATHER",
        "label": "Show weather",
        "defaultValue": "1"
      },
      {
        "type": "toggle",
        "messageKey": "CELSIUS",
        "label": "Celsius (off = Fahrenheit)",
        "defaultValue": "1"
      },
      {
        "type": "select",
        "messageKey": "WEATHER_PROVIDER",
        "label": "Weather provider",
        "defaultValue": "0",
        "description": "Auto uses the server IP to detect location – no GPS or API key needed.",
        "options": [
          { "label": "Auto (kein Setup nötig)",    "value": "0" },
          { "label": "GPS + wttr.in",              "value": "1" },
          { "label": "GPS + OpenWeatherMap (Key)", "value": "2" }
        ]
      },
      {
        "type": "input",
        "label": "OpenWeatherMap API Key (nur bei Option 3)",
        "defaultValue": "",
        "attributes": {
          "placeholder": "e.g. a1b2c3d4e5f6...",
          "type": "text",
          "id": "OWM_API_KEY"
        }
      }
    ]
  },

  /* ─── LANGUAGE ────────────────────────────────────────── */
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Language"
      },
      {
        "type": "select",
        "messageKey": "LANGUAGE",
        "label": "Weekday language",
        "defaultValue": "0",
        "options": [
          { "label": "Deutsch   (Mo Di Mi Do Fr Sa So)", "value": "0" },
          { "label": "English   (Mo Tu We Th Fr Sa Su)", "value": "1" },
          { "label": "Français  (Lu Ma Me Je Ve Sa Di)", "value": "2" },
          { "label": "Español   (Lu Ma Mi Ju Vi Sa Do)", "value": "3" },
          { "label": "Italiano  (Lu Ma Me Gi Ve Sa Do)", "value": "4" }
        ]
      }
    ]
  },

  /* ─── SNAKE ───────────────────────────────────────────── */
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Snake"
      },
      {
        "type": "select",
        "messageKey": "SNAKE_SKIN",
        "label": "Snake skin",
        "defaultValue": "0",
        "options": [
          { "label": "Nokia Classic (green)",    "value": "0" },
          { "label": "Viper (dark emerald)",     "value": "1" },
          { "label": "Python (golden brown)",    "value": "2" },
          { "label": "Ice (teal / blue)",        "value": "3" },
          { "label": "Snake 2 (Nokia original)", "value": "4" },
          { "label": "Neon (cyberpunk)",         "value": "5" },
          { "label": "Nokia 3310 (pointed head)","value": "6" }
        ]
      },
      {
        "type": "select",
        "messageKey": "SNAKE_SPEED",
        "label": "Snake speed (legacy)",
        "defaultValue": "2",
        "options": [
          { "label": "Very Slow",  "value": "0" },
          { "label": "Slow",       "value": "1" },
          { "label": "Normal",     "value": "2" },
          { "label": "Fast",       "value": "3" },
          { "label": "Turbo",      "value": "4" }
        ]
      },
      {
        "type": "select",
        "messageKey": "SNAKE_GROWTH",
        "label": "Growth per food",
        "defaultValue": "1",
        "description": "How many segments the snake gains when eating a food item.",
        "options": [
          { "label": "+1  (short)",  "value": "0" },
          { "label": "+2  (normal)", "value": "1" },
          { "label": "+4  (long)",   "value": "2" },
          { "label": "+8  (giant)",  "value": "3" }
        ]
      },
      {
        "type": "select",
        "messageKey": "STEPS_PER_MIN",
        "label": "Schritte pro Minute",
        "defaultValue": "0",
        "description": "Wie viele Schritte die Schlange pro Minute macht.",
        "options": [
          { "label": "1  (sehr langsam)", "value": "0" },
          { "label": "2  (langsam)",      "value": "1" },
          { "label": "3  (normal)",       "value": "2" },
          { "label": "5  (schnell)",      "value": "3" },
          { "label": "8  (turbo)",        "value": "4" }
        ]
      }
    ]
  },

  /* ─── FOOD ITEMS ──────────────────────────────────────── */
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Food Items"
      },
      {
        "type": "select",
        "messageKey": "ITEM_SET",
        "label": "Food item set",
        "defaultValue": "0",
        "options": [
          { "label": "Fruits (apples, cherries, drops)", "value": "0" },
          { "label": "Nature (mushrooms, stars, gems)",  "value": "1" },
          { "label": "Mixed (all types)",               "value": "2" }
        ]
      }
    ]
  },

  /* ─── SUBMIT ──────────────────────────────────────────── */
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Debug"
      },
      {
        "type": "toggle",
        "messageKey": "ANIM_EVERY_MINUTE",
        "label": "Animate every minute (debug)",
        "description": "Triggers the snake animation at every minute tick instead of only at the full hour.",
        "defaultValue": "0"
      }
    ]
  },

  /* ─── SUBMIT ──────────────────────────────────────────── */
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
