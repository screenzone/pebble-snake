/**
 * Snake 2 Watchface – Phone-side JavaScript
 *
 * Responsibilities:
 *  1. Clay settings UI (auto-generated from config.js)
 *  2. Periodic weather fetching via OpenWeatherMap or wttr.in
 *  3. Sending settings + weather to the watch via AppMessage
 *
 * IMPORTANT – why autoHandleEvents is false:
 *  Clay v1.0.4's clay.getSettings() WITHOUT a response argument throws
 *  ("response.match of undefined") because it always requires the raw
 *  webviewclosed URI.  Additionally, Clay's built-in auto-send forwards
 *  HTML select values as STRINGS (e.g. "1"), but the C side reads them
 *  with t->value->int32, yielding the ASCII byte value (49 for "1") instead
 *  of the integer 1.  Both bugs cause settings to appear silently ignored.
 *
 *  Solution: disable autoHandleEvents, handle showConfiguration and
 *  webviewclosed ourselves, always send INTEGER-typed values.
 */

'use strict';

/* ─── AppMessage keys (must match C defines and package.json order) ─── */
var KEY_TEMPERATURE      = 0;
var KEY_WEATHER_ICON     = 1;
var KEY_COLOR_SCHEME     = 2;
var KEY_LANGUAGE         = 3;
var KEY_SNAKE_SKIN       = 4;
var KEY_SNAKE_SPEED      = 5;
var KEY_ITEM_SET         = 6;
var KEY_CELSIUS          = 7;
var KEY_SHOW_24H         = 8;
var KEY_SHOW_WEATHER     = 9;
var KEY_WEATHER_PROVIDER = 10;
var KEY_ANIM_EVERY_MINUTE = 11;
var KEY_CLOCK_FONT        = 12;
var KEY_SNAKE_GROWTH      = 13;

/* ─── Clay (manual event handling) ──────────────────────────────────── */
var Clay   = require('pebble-clay');
var config = require('./config');
/*
 * autoHandleEvents: false – we register showConfiguration and webviewclosed
 * ourselves so we can ensure all values are sent as JavaScript integers
 * (not strings), which is what the C-side dict_find/int32 path expects.
 */
var clay = new Clay(config, null, { autoHandleEvents: false });

/* ─── Helpers ────────────────────────────────────────────── */

/** Map an OpenWeatherMap weather-id to our 0-7 icon index */
function owm_id_to_icon(id, isNight) {
    if (id >= 200 && id < 300) return 5; /* thunderstorm */
    if (id >= 300 && id < 400) return 3; /* drizzle      */
    if (id >= 500 && id < 600) return 3; /* rain         */
    if (id >= 600 && id < 700) return 4; /* snow         */
    if (id >= 700 && id < 800) return 6; /* atmosphere   */
    if (id === 800) return isNight ? 7 : 0; /* clear    */
    if (id === 801) return 1;            /* few clouds   */
    if (id >= 802 && id < 900) return 2; /* cloudy       */
    return isNight ? 7 : 0;
}

/** Send a message dict to the watch, retrying once on failure */
function send_to_watch(dict) {
    Pebble.sendAppMessage(dict,
        function() { console.log('[Snake2] Message sent OK'); },
        function(e) {
            console.log('[Snake2] Message failed – retrying: ' + JSON.stringify(e));
            setTimeout(function() {
                Pebble.sendAppMessage(dict,
                    function() { console.log('[Snake2] Retry OK'); },
                    function() { console.log('[Snake2] Retry also failed'); }
                );
            }, 1000);
        }
    );
}

/* ─── Settings handling ──────────────────────────────────── */

/*
 * Clay v1.0.4 serialize() produces values in one of two shapes:
 *   - Select / input:  stored in localStorage as plain string  "1"
 *   - Toggle:          stored as boolean  true / false
 *   - Raw Clay object (from getSettings with response): {value: "1"}
 *
 * iv() and bv() unwrap the {value:…} wrapper if present, then coerce to
 * the integer that Pebble.sendAppMessage needs to produce TUPLE_INT on
 * the wire (so the C side can safely read t->value->int32).
 */

/** Parse an integer setting, handling both "1" and {value:"1"} shapes. */
function iv(v, def) {
    if (v !== null && typeof v === 'object' && 'value' in v) { v = v.value; }
    var n = parseInt(v, 10);
    return isNaN(n) ? def : n;
}

/** Parse a boolean setting as 0/1, handling both true and {value:"1"} shapes. */
function bv(v, def) {
    if (v !== null && typeof v === 'object' && 'value' in v) { v = v.value; }
    if (v === undefined || v === null) { return def; }
    return (v === true || v === 'true' || v === '1' || v === 1) ? 1 : 0;
}

/**
 * Build an AppMessage dict from a settings object `s`.
 * All values are JavaScript numbers so Pebble sends them as TUPLE_INT.
 * `s` may be in localStorage flat format {"COLOR_SCHEME":"1"} or in
 * Clay's raw serialised format {"COLOR_SCHEME":{"value":"1"}}.
 */
function build_settings_msg(s) {
    var msg = {};
    msg['COLOR_SCHEME']      = iv(s.COLOR_SCHEME,      0);
    msg['LANGUAGE']          = iv(s.LANGUAGE,          0);
    msg['SNAKE_SKIN']        = iv(s.SNAKE_SKIN,        0);
    msg['SNAKE_SPEED']       = iv(s.SNAKE_SPEED,       2);
    msg['ITEM_SET']          = iv(s.ITEM_SET,          0);
    msg['CELSIUS']           = bv(s.CELSIUS,           1);
    msg['SHOW_24H']          = bv(s.SHOW_24H,          1);
    msg['SHOW_WEATHER']      = bv(s.SHOW_WEATHER,      1);
    msg['WEATHER_PROVIDER']  = iv(s.WEATHER_PROVIDER,  0);
    msg['ANIM_EVERY_MINUTE'] = bv(s.ANIM_EVERY_MINUTE, 0);
    msg['CLOCK_FONT']        = iv(s.CLOCK_FONT,        5);
    msg['SNAKE_GROWTH']      = iv(s.SNAKE_GROWTH,      1);
    msg['STEPS_PER_MIN']     = iv(s.STEPS_PER_MIN,     0);
    console.log('[Snake2] settings → COLOR_SCHEME=' + msg['COLOR_SCHEME'] +
                ' CLOCK_FONT=' + msg['CLOCK_FONT'] +
                ' LANGUAGE='   + msg['LANGUAGE']);
    return msg;
}

/**
 * Read the last-saved settings from localStorage (flat string format
 * written by clay.getSettings()) and push them to the watch.
 * Used from the ready handler to restore settings after reconnect.
 */
function push_settings() {
    var s = {};
    try {
        var raw = localStorage.getItem('clay-settings');
        if (raw) { s = JSON.parse(raw); }
    } catch(e) {
        console.log('[Snake2] localStorage read failed: ' + e);
    }
    send_to_watch(build_settings_msg(s));
}

/* ─── Weather fetching ───────────────────────────────────── */

var s_lat = null;
var s_lon = null;
var s_celsius = true;
var s_weather_provider = 0;  /* 0=auto(wttr.in/IP), 1=GPS+wttr.in, 2=GPS+OWM */
var s_owm_key = '';

/** Read weather-relevant settings from localStorage.
 *  OWM_API_KEY is stored in localStorage only (no messageKey) so it
 *  never travels over AppMessage. */
function get_settings_for_weather() {
    try {
        var raw = localStorage.getItem('clay-settings');
        if (raw) {
            var cs = JSON.parse(raw);
            s_celsius          = (cs.CELSIUS === true || cs.CELSIUS === 'true'
                                  || cs.CELSIUS === '1');
            s_weather_provider = parseInt(cs.WEATHER_PROVIDER, 10) || 0;
            s_owm_key          = cs.OWM_API_KEY || '';
        }
    } catch (e) { /* use defaults */ }
}

/** Convert Kelvin to Celsius */
function k_to_c(k) { return Math.round(k - 273.15); }
/** Convert Celsius to Fahrenheit */
function c_to_f(c) { return Math.round(c * 9 / 5 + 32); }

/**
 * Fetch weather via wttr.in.
 * Pass lat/lon for GPS-accurate location, or null/null to let wttr.in
 * detect location automatically from the phone's IP address (zero config).
 */
function fetch_weather_wttr(lat, lon) {
    var loc = (lat !== null && lon !== null) ? (lat + ',' + lon) : '';
    var url = 'https://wttr.in/' + loc + '?format=j1';
    console.log('[Snake2] wttr.in → ' + url);
    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
        if (xhr.status !== 200) {
            console.log('[Snake2] wttr.in HTTP ' + xhr.status);
            return;
        }
        try {
            var resp = JSON.parse(xhr.responseText);
            var temp_c = parseInt(resp.current_condition[0].temp_C, 10);
            var temp   = s_celsius ? temp_c : c_to_f(temp_c);
            /*
             * Map WMO weather interpretation codes (used by wttr.in) to icons.
             * Full code table: https://www.nodc.noaa.gov/archive/arc0021/0002199/1.1/data/0-data/HTML/WMO-CODE/WMO4677.HTM
             *  113        = Clear / Sunny              → 0 sun
             *  116        = Partly cloudy              → 1 partly cloudy
             *  119 / 122  = Cloudy / Overcast          → 2 cloud
             *  143 / 248 / 260 = Mist / Fog            → 6 fog
             *  200 / 386-395  = Thunder                → 5 storm
             *  179-230 / 323-377 = Snow / Blizzard     → 4 snow
             *  176 / 263-320 / 353-365 = Rain/Drizzle  → 3 rain
             */
            var wcode = parseInt(resp.current_condition[0].weatherCode, 10);
            var icon;
            if (wcode === 113) {
                icon = 0; /* sun */
            } else if (wcode === 116) {
                icon = 1; /* partly cloudy */
            } else if (wcode === 119 || wcode === 122) {
                icon = 2; /* cloudy */
            } else if (wcode === 143 || wcode === 248 || wcode === 260) {
                icon = 6; /* fog */
            } else if (wcode === 200 || wcode >= 386) {
                icon = 5; /* thunder */
            } else if ((wcode >= 179 && wcode <= 230) ||
                       (wcode >= 323 && wcode <= 338) ||
                       wcode === 350 ||
                       (wcode >= 368 && wcode <= 377)) {
                icon = 4; /* snow / blizzard / ice */
            } else {
                icon = 3; /* rain / drizzle / sleet (all remaining codes) */
            }
            var msg = {};
            msg['TEMPERATURE']  = temp;
            msg['WEATHER_ICON'] = icon;
            send_to_watch(msg);
        } catch(e) {
            console.log('[Snake2] wttr.in parse error: ' + e);
        }
    };
    xhr.onerror = function() { console.log('[Snake2] wttr.in network error'); };
    xhr.open('GET', url);
    xhr.setRequestHeader('User-Agent', 'Snake2-Pebble-Watchface');
    xhr.send();
}

function fetch_weather_owm(lat, lon) {
    if (!s_owm_key) {
        console.log('[Snake2] No OWM API key – falling back to wttr.in auto');
        fetch_weather_wttr(null, null);
        return;
    }
    var url = 'https://api.openweathermap.org/data/2.5/weather' +
              '?lat=' + lat + '&lon=' + lon +
              '&appid=' + encodeURIComponent(s_owm_key);
    console.log('[Snake2] OWM request');
    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
        if (xhr.status !== 200) {
            console.log('[Snake2] OWM HTTP ' + xhr.status + ' – falling back to wttr.in auto');
            fetch_weather_wttr(null, null);
            return;
        }
        try {
            var resp = JSON.parse(xhr.responseText);
            var temp_c = k_to_c(resp.main.temp);
            var temp   = s_celsius ? temp_c : c_to_f(temp_c);
            var is_night = (resp.dt < resp.sys.sunrise || resp.dt > resp.sys.sunset);
            var icon   = owm_id_to_icon(resp.weather[0].id, is_night);
            var d = {};
            d['TEMPERATURE']  = temp;
            d['WEATHER_ICON'] = icon;
            send_to_watch(d);
        } catch(e) {
            console.log('[Snake2] OWM parse error: ' + e);
            fetch_weather_wttr(null, null);
        }
    };
    xhr.onerror = function() {
        console.log('[Snake2] OWM network error – falling back to wttr.in auto');
        fetch_weather_wttr(null, null);
    };
    xhr.open('GET', url);
    xhr.send();
}

/**
 * Fetch weather according to the configured provider:
 *   0 = Auto:         wttr.in with IP-based location (no permissions needed)
 *   1 = GPS+wttr.in:  acquire GPS coordinates, then call wttr.in
 *   2 = GPS+OWM:      acquire GPS coordinates, then call OpenWeatherMap
 * For providers 1 & 2, if GPS fails the fetch falls back to Auto (provider 0).
 */
function fetch_weather() {
    get_settings_for_weather();

    /* Provider 0: fully automatic, no GPS or API key required */
    if (s_weather_provider === 0) {
        fetch_weather_wttr(null, null);
        return;
    }

    /* Providers 1 & 2 need GPS coordinates */
    if (s_lat === null || s_lon === null) {
        navigator.geolocation.getCurrentPosition(
            function(pos) {
                s_lat = pos.coords.latitude;
                s_lon = pos.coords.longitude;
                if (s_weather_provider === 2) {
                    fetch_weather_owm(s_lat, s_lon);
                } else {
                    fetch_weather_wttr(s_lat, s_lon);
                }
            },
            function(err) {
                /* GPS unavailable – fall back to IP-based auto mode */
                console.log('[Snake2] GPS error: ' + err.message + ' – using IP location');
                fetch_weather_wttr(null, null);
            },
            { timeout: 15000, maximumAge: 600000 }
        );
        return;
    }

    if (s_weather_provider === 2) {
        fetch_weather_owm(s_lat, s_lon);
    } else {
        fetch_weather_wttr(s_lat, s_lon);
    }
}

/* ─── Pebble events ──────────────────────────────────────── */

/*
 * showConfiguration: open the Clay config page in the Pebble app.
 * Must be handled manually since autoHandleEvents is false.
 */
Pebble.addEventListener('showConfiguration', function() {
    Pebble.openURL(clay.generateUrl());
});

/*
 * webviewclosed: user saved the Clay settings UI.
 *
 * clay.getSettings(e.response, false) parses the raw webview URI,
 * saves the flat version to localStorage, and returns the RAW object
 * with values in {value:"1"} shape (convert=false).
 * build_settings_msg() unwraps those objects and coerces everything
 * to JavaScript integers before sending, guaranteeing TUPLE_INT on
 * the wire.  The C side can then safely read t->value->int32.
 */
Pebble.addEventListener('webviewclosed', function(e) {
    console.log('[Snake2] webviewclosed');
    if (!e || !e.response) {
        console.log('[Snake2] No response – skipping settings send');
        fetch_weather();
        return;
    }
    var s = {};
    try {
        /* Pass convert=false to get the raw {value:…} objects.
           Clay also saves the flat version to localStorage as a side effect. */
        s = clay.getSettings(e.response, false) || {};
    } catch(err) {
        console.log('[Snake2] getSettings error: ' + err);
        /* Fallback: localStorage was just updated by Clay's internal path */
        try {
            var raw = localStorage.getItem('clay-settings');
            if (raw) { s = JSON.parse(raw); }
        } catch(e2) { /* use defaults */ }
    }
    send_to_watch(build_settings_msg(s));
    fetch_weather();
});

Pebble.addEventListener('ready', function() {
    console.log('[Snake2] JS ready');
    /*
     * Push persisted settings so the watch is up-to-date after a
     * Bluetooth reconnect or watchface reinstall.
     * Reads from localStorage (written by Clay on previous config saves).
     */
    push_settings();
    fetch_weather();
    /* Refresh weather every 15 minutes */
    setInterval(fetch_weather, 15 * 60 * 1000);
});

Pebble.addEventListener('appmessage', function(e) {
    console.log('[Snake2] Message from watch: ' + JSON.stringify(e.payload));
});
