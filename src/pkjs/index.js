// PebbleKit JS: Clay-Einstellungen mit Farbvorlagen plus Wetter von Open-Meteo.
// Die Uhr fragt alle 30 Minuten per REQUEST_WEATHER an; beim Start und nach
// dem Speichern der Einstellungen wird von hier aus selbst abgefragt.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');

var clay = new Clay(clayConfig, customClay);

var WEATHER_URL = 'https://api.open-meteo.com/v1/forecast';
var REQUEST_TIMEOUT_MS = 15000;
var SEND_DELAY_MS = 1500;   // nach Clays eigener Nachricht, sonst "busy"

// Zustandscodes = Position in der Sprite-Leiste, muessen zu shore.c und
// tools/make_icons.py passen
var COND_CLEAR = 0, COND_CLEAR_NIGHT = 1, COND_PARTLY = 2, COND_PARTLY_NIGHT = 3,
    COND_CLOUDY = 4, COND_FOG = 5, COND_DRIZZLE = 6, COND_RAIN = 7,
    COND_SHOWERS = 8, COND_SNOW = 9, COND_SLEET = 10, COND_STORM = 11;

// WMO-Wettercodes auf die zwoelf Symbole der Uhr
function conditionFor(code, isDay) {
    if (code === 0)                      return isDay ? COND_CLEAR : COND_CLEAR_NIGHT;
    if (code === 1 || code === 2)        return isDay ? COND_PARTLY : COND_PARTLY_NIGHT;
    if (code === 3)                      return COND_CLOUDY;
    if (code === 45 || code === 48)      return COND_FOG;
    if (code >= 51 && code <= 55)        return COND_DRIZZLE;
    if (code === 56 || code === 57)      return COND_SLEET;
    if (code >= 61 && code <= 65)        return COND_RAIN;
    if (code === 66 || code === 67)      return COND_SLEET;
    if (code >= 71 && code <= 77)        return COND_SNOW;
    if (code >= 80 && code <= 82)        return COND_SHOWERS;
    if (code === 85 || code === 86)      return COND_SNOW;
    if (code >= 95)                      return COND_STORM;
    return COND_CLOUDY;
}

function getStoredSettings() {
    try {
        return JSON.parse(localStorage.getItem('clay-settings') || '{}');
    } catch (e) {
        return {};
    }
}

// Keine Schnittstelle zur Einheiten-Einstellung der Pebble-App, also die
// Region des Telefons auswerten -- wer damit falsch liegt, stellt es um.
var FAHRENHEIT_REGIONS = /-(US|BS|BZ|KY|FM|MH|PW|LR)\b/i;

function useFahrenheit(settings) {
    if (settings.TEMP_UNIT === 'celsius')    return false;
    if (settings.TEMP_UNIT === 'fahrenheit') return true;
    var locale = (navigator && navigator.language) || '';
    return FAHRENHEIT_REGIONS.test(locale);
}

function fetchWeather(lat, lon) {
    var settings = getStoredSettings();
    var daily = settings.WEATHER_SYMBOL === 'today';
    var url = WEATHER_URL
            + '?latitude='  + lat.toFixed(4)
            + '&longitude=' + lon.toFixed(4)
            + '&current=weather_code,is_day'
            + '&daily=weather_code,temperature_2m_max,temperature_2m_min'
            + '&timezone=auto&forecast_days=1'
            + '&temperature_unit=' + (useFahrenheit(settings) ? 'fahrenheit' : 'celsius');

    var request = new XMLHttpRequest();
    request.open('GET', url, true);
    request.timeout = REQUEST_TIMEOUT_MS;
    request.onload = function() {
        if (request.status !== 200) {
            console.log('weather request failed: HTTP ' + request.status);
            return;
        }
        try {
            var data = JSON.parse(request.responseText) || {};
            var current = data.current, day = data.daily;
            if (!current || !day || !day.temperature_2m_max) return;
            var cond = daily ? conditionFor(day.weather_code[0], true)
                             : conditionFor(current.weather_code, current.is_day);
            Pebble.sendAppMessage({
                'CONDITION': cond,
                'TEMP_LO': Math.round(day.temperature_2m_min[0]),
                'TEMP_HI': Math.round(day.temperature_2m_max[0])
            }, null, function() { console.log('weather: send failed'); });
        } catch (e) {
            console.log('weather not readable: ' + e);
        }
    };
    request.onerror   = function() { console.log('weather: network error'); };
    request.ontimeout = function() { console.log('weather: timeout'); };
    request.send();
}

function refreshWeather() {
    navigator.geolocation.getCurrentPosition(
        function(pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
        function(err) { console.log('location unavailable: ' + err.message); },
        { enableHighAccuracy: false, maximumAge: 1800000, timeout: 15000 }
    );
}

Pebble.addEventListener('ready', function() {
    refreshWeather();
});

Pebble.addEventListener('appmessage', function(e) {
    if (e.payload && e.payload.REQUEST_WEATHER !== undefined) refreshWeather();
});

// Feuert nach Clays Handler; Einheit oder Symbolart koennen sich geaendert haben
Pebble.addEventListener('webviewclosed', function(e) {
    if (e && e.response && e.response !== 'CANCELLED') {
        setTimeout(refreshWeather, SEND_DELAY_MS);
    }
});
