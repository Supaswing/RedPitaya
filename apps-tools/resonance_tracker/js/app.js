(function (tracker) {
    document.querySelectorAll('[data-view]').forEach(function (button) {
        button.addEventListener('click', function () {
            tracker.setActiveView(button.getAttribute('data-view'));
        });
    });
    tracker.setActiveView('stopped');
    window.setTimeout(tracker.transport.start, 250);
}(window.ResonanceTracker));
