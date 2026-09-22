(function (tracker) {
    tracker.registerDashboard('tracking', {
        enter: function (snapshot) { this.update(snapshot); },
        update: function () {
            document.getElementById('tracking-summary').textContent =
                'Backend state: ' + tracker.stateName(tracker.store.instrumentState) + '. Continuous tracking belongs to Milestone 2B.';
        },
        leave: function () {}
    });
}(window.ResonanceTracker));
