(function (tracker) {
    const windowShift = document.getElementById('window-shift');
    windowShift.addEventListener('change', function () {
        const value = Number(windowShift.value);
        windowShift.dataset.pending = String(value);
        if (!tracker.transport.send({RT_WINDOW_SHIFT: {value: value}})) delete windowShift.dataset.pending;
    });
    tracker.setActiveView('stopped');
    window.setTimeout(tracker.transport.start, 250);
}(window.ResonanceTracker));
