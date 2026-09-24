(function (tracker) {
    const enabled = [document.getElementById('sensor-enable-1'), document.getElementById('sensor-enable-2')];
    const shown = [document.getElementById('sensor-show-1'), document.getElementById('sensor-show-2')];
    let pendingMask = null;
    tracker.sensorVisibilityVersion = 0;

    tracker.sensorVisible = function (id) {
        return id >= 1 && id <= 2 && shown[id - 1].checked;
    };

    function mask() { return (enabled[0].checked ? 1 : 0) | (enabled[1].checked ? 2 : 0); }
    enabled.forEach(function (input) {
        input.addEventListener('change', function () {
            pendingMask = mask();
            if (!tracker.transport.send({RT_SENSOR_ENABLE_MASK: {value: pendingMask}})) pendingMask = null;
        });
    });
    shown.forEach(function (input) {
        input.addEventListener('change', function () {
            tracker.sensorVisibilityVersion += 1;
            tracker.publish({}, {});
        });
    });

    tracker.updateSensorControls = function () {
        const backendMask = Number(tracker.parameter('RT_SENSOR_ENABLE_MASK', 1));
        if (pendingMask === backendMask) pendingMask = null;
        if (pendingMask === null) enabled.forEach(function (input, index) {
            input.checked = Boolean(backendMask & (1 << index));
        });
        const state = Number(tracker.parameter('RT_STATE', 0));
        const busy = state === 2 || state === 4 || state === 5 || (state >= 6 && state <= 9);
        enabled.forEach(function (input) { input.disabled = busy; });
    };
}(window.ResonanceTracker));
