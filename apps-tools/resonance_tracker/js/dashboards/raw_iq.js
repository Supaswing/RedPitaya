(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;

    function number(id, parameter, digits, available) {
        const value = Number(tracker.parameter(parameter, NaN));
        byId(id).textContent = available && Number.isFinite(value) ? value.toFixed(digits) : '-';
    }

    function draw(snapshot) {
        const canvas = byId('plot');
        const context = canvas.getContext('2d');
        const incident = snapshot.signals.RT_HISTORY_INC_MAG;
        const reflected = snapshot.signals.RT_HISTORY_REF_MAG;
        context.clearRect(0, 0, canvas.width, canvas.height);
        if (!incident || !Array.isArray(incident.value) || incident.value.length < 2) return;
        const reflectedValues = reflected && Array.isArray(reflected.value) ? reflected.value : [];
        const all = incident.value.concat(reflectedValues).filter(Number.isFinite);
        const maximum = Math.max.apply(null, all) || 1;
        [['#d5f36a', incident.value], ['#69d7c6', reflectedValues]].forEach(function (line) {
            if (line[1].length < 2) return;
            context.strokeStyle = line[0];
            context.lineWidth = 2;
            context.beginPath();
            line[1].forEach(function (point, index) {
                const x = index / (line[1].length - 1) * canvas.width;
                const y = canvas.height - 12 - point / maximum * (canvas.height - 24);
                if (index) context.lineTo(x, y);
                else context.moveTo(x, y);
            });
            context.stroke();
        });
    }

    function bindControls() {
        if (controlsBound) return;
        controlsBound = true;
        byId('run').addEventListener('click', function () {
            const requestedRun = !Boolean(tracker.parameter('RT_RUN', false));
            if (tracker.transport.send({RT_RUN: {value: requestedRun}})) {
                byId('run').textContent = requestedRun ? 'STARTING' : 'STOPPING';
                byId('run').disabled = true;
                window.setTimeout(function () { byId('run').disabled = false; }, 500);
            }
        });
        byId('frequency').addEventListener('change', function () {
            tracker.transport.send({RT_FREQUENCY_HZ: {value: Number(byId('frequency').value)}});
        });
        byId('window-shift').addEventListener('change', function () {
            tracker.transport.send({RT_WINDOW_SHIFT: {value: Number(byId('window-shift').value)}});
        });
        byId('interval').addEventListener('change', function () {
            tracker.transport.send({RT_TELEMETRY_MS: {value: Number(byId('interval').value)}});
        });
    }

    function update(snapshot) {
        const running = Boolean(tracker.parameter('RT_RUN', false));
        const state = Number(tracker.parameter('RT_STATE', 0));
        byId('state').textContent = tracker.stateName(state);
        byId('error').textContent = tracker.parameter('RT_ERROR', '') || 'No backend error';
        byId('sequence').textContent = tracker.parameter('RT_SEQUENCE', 0);
        byId('validity').textContent = tracker.parameter('RT_VALID', false) ? 'VALID' : 'INVALID';
        byId('busy').textContent = tracker.parameter('RT_BUSY', false) ? 'BUSY' : 'IDLE';
        byId('overflow').textContent = tracker.parameter('RT_OVERFLOW', false) ? 'OVERFLOW' : 'OVERFLOW: UNAVAILABLE';
        byId('requested').textContent = tracker.parameter('RT_REQUESTED_FREQUENCY_HZ', '-');
        byId('effective').textContent = tracker.parameter('RT_EFFECTIVE_FREQUENCY_HZ', '-');
        number('rate', 'RT_ACQUISITION_RATE_HZ', 1, true);
        number('publication-rate', 'RT_PUBLICATION_RATE_HZ', 1, true);
        byId('run').textContent = running ? 'STOP' : 'RUN';
        byId('run').className = running ? 'stop' : 'run';
        byId('run').disabled = state !== 0 && state !== 1 && state !== 10;
        if (document.activeElement !== byId('frequency')) byId('frequency').value = tracker.parameter('RT_FREQUENCY_HZ', 32000000);
        if (document.activeElement !== byId('window-shift')) byId('window-shift').value = tracker.parameter('RT_WINDOW_SHIFT', 17);
        if (document.activeElement !== byId('interval')) byId('interval').value = tracker.parameter('RT_TELEMETRY_MS', 50);
        number('integration-samples', 'RT_INTEGRATION_SAMPLES', 0, true);
        number('integration-time', 'RT_INTEGRATION_TIME_US', 3, true);
        byId('effective-window-shift').textContent = tracker.parameter('RT_EFFECTIVE_WINDOW_SHIFT', 17);
        byId('period-count').textContent = tracker.parameter('RT_PERIOD_COUNT', 0);

        [['inc-i','RT_INC_I'],['inc-q','RT_INC_Q'],['inc-mag','RT_INC_MAG'],['inc-phase','RT_INC_PHASE_DEG'],['ref-i','RT_REF_I'],['ref-q','RT_REF_Q'],['ref-mag','RT_REF_MAG'],['ref-phase','RT_REF_PHASE_DEG']].forEach(function (entry) {
            const value = tracker.parameter(entry[1], '-');
            byId(entry[0]).textContent = typeof value === 'number' ? value.toFixed(entry[1].includes('PHASE') || entry[1].includes('MAG') ? 3 : 0) : value;
        });

        const sampleCount = Number(tracker.parameter('RT_STATS_COUNT', 0));
        const ratioCount = Number(tracker.parameter('RT_RATIO_STATS_COUNT', 0));
        const ratioValid = Boolean(tracker.parameter('RT_R_VALID', false));
        byId('stats-count').textContent = sampleCount;
        byId('ratio-stats-count').textContent = ratioCount;
        byId('ratio-validity').textContent = ratioValid ? 'VALID' : 'INVALID';
        [['ratio-real','RT_R_REAL'],['ratio-imag','RT_R_IMAG'],['ratio-mag','RT_R_MAG'],['ratio-phase','RT_R_PHASE_DEG']].forEach(function (entry) { number(entry[0], entry[1], 6, ratioValid); });
        [['inc-i-mean','RT_INC_I_MEAN'],['inc-i-stddev','RT_INC_I_STDDEV'],['inc-q-mean','RT_INC_Q_MEAN'],['inc-q-stddev','RT_INC_Q_STDDEV'],['ref-i-mean','RT_REF_I_MEAN'],['ref-i-stddev','RT_REF_I_STDDEV'],['ref-q-mean','RT_REF_Q_MEAN'],['ref-q-stddev','RT_REF_Q_STDDEV']].forEach(function (entry) { number(entry[0], entry[1], 2, sampleCount > 0); });
        [['ratio-real-mean','RT_R_REAL_MEAN'],['ratio-real-stddev','RT_R_REAL_STDDEV'],['ratio-imag-mean','RT_R_IMAG_MEAN'],['ratio-imag-stddev','RT_R_IMAG_STDDEV'],['ratio-mag-mean','RT_R_MAG_MEAN'],['ratio-mag-stddev','RT_R_MAG_STDDEV'],['ratio-mean-phase','RT_R_MEAN_PHASE_DEG']].forEach(function (entry) { number(entry[0], entry[1], 6, ratioCount > 0); });
        draw(snapshot);
    }

    tracker.registerDashboard('raw', {
        enter: function (snapshot) { bindControls(); update(snapshot); },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
