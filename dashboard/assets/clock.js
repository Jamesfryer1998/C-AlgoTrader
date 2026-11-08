// The status bar's clocks, ticking in the browser (no request to the dashboard each second).
// Dash loads every file in assets/ by itself; app.py wires this up with a clientside callback.
const NEW_YORK_ZONE = "America/New_York";
const NEW_YORK_LABEL = "NEW YORK ";
const LOCAL_LABEL = "LOCAL ";
const TIME_FORMAT = {hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23"};

window.dash_clientside = Object.assign({}, window.dash_clientside, {
    clock: {
        tick: function () {
            const now = new Date();
            const newYork = now.toLocaleTimeString("en-GB", {...TIME_FORMAT, timeZone: NEW_YORK_ZONE});
            const local = now.toLocaleTimeString("en-GB", TIME_FORMAT);
            return [NEW_YORK_LABEL + newYork, LOCAL_LABEL + local];
        }
    }
});
