(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;
    let lastSequence = -1;
    const pendingConfiguration = {};

    function values(name) {
        const signal = tracker.store.signals[name];
        return signal && Array.isArray(signal.value) ? signal.value : [];
    }

    function draw() {
        const signalSequence = values('RT_BASELINE_SIGNAL_SEQUENCE');
        const parameterSequence = Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0));
        const frequency = values('RT_BASELINE_FREQUENCY');
        const real = values('RT_BASELINE_RE');
        const imag = values('RT_BASELINE_IM');
        const canvas = byId('baseline-plot');
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, canvas.width, canvas.height);
        if (signalSequence.length !== 1 || Number(signalSequence[0]) !== parameterSequence ||
            frequency.length < 2 || real.length !== frequency.length || imag.length !== frequency.length) return false;
        const magnitude = real.map(function (value, index) { return Math.hypot(value, imag[index]); });
        const minimum = Math.min.apply(null, magnitude);
        const maximum = Math.max.apply(null, magnitude);
        const span = maximum - minimum || 1;
        context.strokeStyle = '#d5f36a';
        context.lineWidth = 2;
        context.beginPath();
        magnitude.forEach(function (value, index) {
            const x = index / (magnitude.length - 1) * canvas.width;
            const y = canvas.height - 15 - (value - minimum) / span * (canvas.height - 30);
            if (index) context.lineTo(x, y); else context.moveTo(x, y);
        });
        context.stroke();
        return true;
    }

    function format(id, name, digits, valid) {
        const value = Number(tracker.parameter(name, NaN));
        byId(id).textContent = valid && Number.isFinite(value) ? value.toFixed(digits) : '-';
    }

    function sendConfiguration(inputId, parameterName) {
        const value = Number(byId(inputId).value);
        pendingConfiguration[parameterName] = value;
        const message = {};
        message[parameterName] = {value: value};
        if (!tracker.transport.send(message)) delete pendingConfiguration[parameterName];
    }

    function synchronizeInput(inputId, parameterName, fallback) {
        const backendValue = Number(tracker.parameter(parameterName, fallback));
        if (Object.prototype.hasOwnProperty.call(pendingConfiguration, parameterName)) {
            if (backendValue === pendingConfiguration[parameterName]) delete pendingConfiguration[parameterName];
            else return;
        }
        if (document.activeElement !== byId(inputId)) byId(inputId).value = backendValue;
    }

    function bindControls() {
        if (controlsBound) return;
        controlsBound = true;
        byId('baseline-start-button').addEventListener('click', function () {
            tracker.sendCommand(1, {
                RT_BASELINE_START_HZ: {value: Number(byId('baseline-start').value)},
                RT_BASELINE_STOP_HZ: {value: Number(byId('baseline-stop').value)},
                RT_BASELINE_SENSOR_COUNT: {value: Number(byId('baseline-sensors').value)}
            });
        });
        byId('baseline-cancel-button').addEventListener('click', function () { tracker.sendCommand(2); });
        byId('baseline-start').addEventListener('change', function () { sendConfiguration('baseline-start', 'RT_BASELINE_START_HZ'); });
        byId('baseline-stop').addEventListener('change', function () { sendConfiguration('baseline-stop', 'RT_BASELINE_STOP_HZ'); });
        byId('baseline-sensors').addEventListener('change', function () { sendConfiguration('baseline-sensors', 'RT_BASELINE_SENSOR_COUNT'); });
    }

    function update() {
        const valid = Boolean(tracker.parameter('RT_BASELINE_VALID', false));
        const complete = Boolean(tracker.parameter('RT_BASELINE_COMPLETE', false));
        const progress = Number(tracker.parameter('RT_BASELINE_PROGRESS', 0));
        const state = Number(tracker.parameter('RT_STATE', 0));
        const active = state === 2 || state === 4;
        byId('baseline-summary').textContent = valid ? 'Last completed baseline is valid.' :
            (active ? 'Baseline acquisition and resonance finding are active.' :
                (complete ? 'The completed baseline was rejected; see the backend error.' : 'No completed baseline is available.'));
        byId('baseline-progress').textContent = progress.toFixed(1);
        byId('baseline-start-button').disabled = active || state === 5;
        byId('baseline-cancel-button').disabled = !active;
        byId('baseline-start').disabled = active;
        byId('baseline-stop').disabled = active;
        byId('baseline-sensors').disabled = active;
        synchronizeInput('baseline-start', 'RT_BASELINE_START_HZ', 30000000);
        synchronizeInput('baseline-stop', 'RT_BASELINE_STOP_HZ', 34000000);
        synchronizeInput('baseline-sensors', 'RT_BASELINE_SENSOR_COUNT', 1);
        format('resonance-frequency', 'RT_RESONANCE_FREQUENCY_HZ', 1, valid);
        format('resonance-q', 'RT_RESONANCE_Q', 3, valid);
        format('resonance-fwhm', 'RT_RESONANCE_FWHM_HZ', 1, valid);
        format('resonance-se', 'RT_RESONANCE_SE_HZ', 1, valid);
        if (valid && !Boolean(tracker.parameter('RT_RESONANCE_MODEL_VALID', false)))
            byId('resonance-quality').textContent = 'CURVATURE FALLBACK';
        else
            format('resonance-quality', 'RT_RESONANCE_MODEL_QUALITY', 4, valid);
        const sequence = Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0));
        if (sequence !== lastSequence && draw()) lastSequence = sequence;
    }

    tracker.registerDashboard('baseline', {
        enter: function () { bindControls(); update(); },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
