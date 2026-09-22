(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;
    let lastSequence = -1;

    function values(name) {
        const signal = tracker.store.signals[name];
        return signal && Array.isArray(signal.value) ? signal.value : [];
    }

    function draw() {
        const signalSequence = values('RT_DIAG_SIGNAL_SEQUENCE');
        const parameterSequence = Number(tracker.parameter('RT_DIAG_SEQUENCE', 0));
        const offset = values('RT_DIAG_OFFSET');
        const frequency = values('RT_DIAG_FREQUENCY_HZ');
        const real = values('RT_DIAG_RE');
        const imag = values('RT_DIAG_IM');
        const canvas = byId('diagnostics-plot');
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, canvas.width, canvas.height);
        const body = byId('diagnostics-table');
        body.textContent = '';
        if (signalSequence.length !== 1 || Number(signalSequence[0]) !== parameterSequence ||
            !real.length || imag.length !== real.length) return false;
        const xMin = Math.min.apply(null, real), xMax = Math.max.apply(null, real);
        const yMin = Math.min.apply(null, imag), yMax = Math.max.apply(null, imag);
        const xSpan = xMax - xMin || 1, ySpan = yMax - yMin || 1;
        context.strokeStyle = '#69d7c6';
        context.fillStyle = '#d5f36a';
        context.lineWidth = 2;
        context.beginPath();
        real.forEach(function (value, index) {
            const x = 20 + (value - xMin) / xSpan * (canvas.width - 40);
            const y = canvas.height - 20 - (imag[index] - yMin) / ySpan * (canvas.height - 40);
            if (index) context.lineTo(x, y); else context.moveTo(x, y);
            context.fillRect(x - 3, y - 3, 6, 6);
            const row = document.createElement('tr');
            [offset[index], frequency[index], value, imag[index]].forEach(function (cellValue, column) {
                const cell = document.createElement(column === 0 ? 'th' : 'td');
                cell.textContent = Number(cellValue).toFixed(column < 2 ? 0 : 7);
                row.appendChild(cell);
            });
            body.appendChild(row);
        });
        context.stroke();
        return true;
    }

    function bindControls() {
        if (controlsBound) return;
        controlsBound = true;
        byId('diagnostics-start-button').addEventListener('click', function () { tracker.sendCommand(3); });
        byId('diagnostics-cancel-button').addEventListener('click', function () { tracker.sendCommand(4); });
    }

    function update() {
        const complete = Boolean(tracker.parameter('RT_DIAG_COMPLETE', false));
        const state = Number(tracker.parameter('RT_STATE', 0));
        byId('diagnostics-summary').textContent = complete ? 'Displaying the last complete diagnostic point set.' : 'No complete diagnostic point set is available.';
        byId('diagnostics-sensor').textContent = tracker.parameter('RT_DIAG_SENSOR_ID', '-');
        byId('diagnostics-sequence').textContent = tracker.parameter('RT_DIAG_SEQUENCE', '-');
        byId('diagnostics-start-button').disabled = state === 2 || state === 4 || state === 5 || !Boolean(tracker.parameter('RT_BASELINE_VALID', false));
        byId('diagnostics-cancel-button').disabled = state !== 5;
        const sequence = Number(tracker.parameter('RT_DIAG_SEQUENCE', 0));
        if (sequence !== lastSequence && draw()) lastSequence = sequence;
    }

    tracker.registerDashboard('diagnostics', {
        enter: function () { bindControls(); update(); },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
