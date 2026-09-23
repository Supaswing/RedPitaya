(function (global) {
    const tracker = global.ResonanceTracker = global.ResonanceTracker || {};
    const stateNames = ['STOPPED','RAW_IQ','BASELINE_ACQUIRING','BASELINE_READY','RESONANCE_FINDING','DIAGNOSTICS','SEARCHING','TRACKING','DEGRADED','RELOCKING','ERROR'];
    const defaultViews = ['stopped','raw','baseline','baseline','baseline','diagnostics','tracking','tracking','tracking','tracking',null];
    const store = {
        params: {},
        signals: {},
        activeView: null,
        instrumentState: null,
        dashboards: {}
    };

    function parameter(name, fallback) {
        const item = store.params[name];
        return item && item.value !== undefined ? item.value : fallback;
    }

    function stateName(value) {
        return stateNames[value] || 'UNKNOWN';
    }

    function setActiveView(name) {
        const next = store.dashboards[name];
        if (!next || store.activeView === name) return;
        const previous = store.dashboards[store.activeView];
        if (previous && previous.leave) previous.leave();
        Object.keys(store.dashboards).forEach(function (dashboardName) {
            document.getElementById('dashboard-' + dashboardName).hidden = dashboardName !== name;
        });
        document.querySelectorAll('[data-view]').forEach(function (button) {
            button.classList.toggle('active', button.getAttribute('data-view') === name);
        });
        store.activeView = name;
        if (next.enter) next.enter(store);
    }

    function updateGlobalStatus() {
        const nextState = Number(parameter('RT_STATE', 0));
        document.getElementById('instrument-state').textContent = stateName(nextState);
        const error = String(parameter('RT_ERROR', '') || '');
        const banner = document.getElementById('global-error');
        banner.hidden = !error;
        banner.textContent = error;
        const windowShift = document.getElementById('window-shift');
        const requestedShift = Number(parameter('RT_WINDOW_SHIFT', 17));
        if (windowShift.dataset.pending !== undefined) {
            if (requestedShift === Number(windowShift.dataset.pending)) delete windowShift.dataset.pending;
        }
        if (windowShift.dataset.pending === undefined && document.activeElement !== windowShift)
            windowShift.value = requestedShift;
        windowShift.disabled = nextState === 2 || nextState === 4 || nextState === 5 ||
            (nextState >= 6 && nextState <= 9);
        document.getElementById('global-integration-samples').textContent =
            Number(parameter('RT_INTEGRATION_SAMPLES', 131072)).toFixed(0);
        if (nextState !== store.instrumentState) {
            store.instrumentState = nextState;
            if (nextState !== 10 && defaultViews[nextState] && store.activeView !== 'tuning')
                setActiveView(defaultViews[nextState]);
        }
    }

    tracker.registerDashboard = function (name, dashboard) {
        store.dashboards[name] = dashboard;
    };
    tracker.setActiveView = setActiveView;
    tracker.parameter = parameter;
    tracker.stateName = stateName;
    let commandSequence = 0;
    tracker.sendCommand = function (command, extraParameters) {
        commandSequence = Math.max(commandSequence, Number(parameter('RT_COMMAND_ACK', 0))) + 1;
        const values = Object.assign({}, extraParameters || {});
        values.RT_COMMAND = {value: command};
        values.RT_COMMAND_SEQUENCE = {value: commandSequence};
        return tracker.transport.send(values);
    };
    tracker.store = store;
    tracker.publish = function (parameters, signals) {
        Object.assign(store.params, parameters || {});
        Object.assign(store.signals, signals || {});
        updateGlobalStatus();
        const active = store.dashboards[store.activeView];
        if (active && active.update) active.update(store);
    };
}(window));
