(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let bound = false;
    let lastKey = '';

    function values(name) {
        const signal = tracker.store.signals[name];
        return signal && Array.isArray(signal.value) ? signal.value : [];
    }

    function frame(prefix, sequenceName, completeName, pointCount, sensorCount) {
        const sequence = Number(tracker.parameter(sequenceName, 0));
        const suffix = prefix === 'RT_DIAG_' ? '' : 'POINT_';
        const ids = values(prefix + 'POINT_SENSOR_ID');
        const offsets = values(prefix + suffix + 'OFFSET');
        const frequencies = values(prefix + suffix + 'FREQUENCY_HZ');
        const real = values(prefix + suffix + 'RE');
        const imag = values(prefix + suffix + 'IM');
        const expected = pointCount * sensorCount;
        const signalSequence = values(prefix + 'SIGNAL_SEQUENCE');
        if (!tracker.parameter(completeName, false) || sequence <= 0 ||
            signalSequence.length !== 1 || Number(signalSequence[0]) !== sequence || expected < 1 ||
            [ids, offsets, frequencies, real, imag].some(function (array) { return array.length !== expected; })) return null;
        const seen = new Set();
        const points = [];
        for (let i = 0; i < expected; ++i) {
            const sensor = Number(ids[i]), offset = Number(offsets[i]);
            const key = sensor + ':' + offset;
            if ((sensor !== 1 && sensor !== 2) || seen.has(key) ||
                ![offset, frequencies[i], real[i], imag[i]].every(function (v) { return Number.isFinite(Number(v)); })) return null;
            seen.add(key);
            points.push({sensor: sensor, offset: offset, frequency: Number(frequencies[i]),
                real: Number(real[i]), imag: Number(imag[i])});
        }
        const uniqueSensors = new Set(points.map(function (point) { return point.sensor; }));
        if (uniqueSensors.size !== sensorCount || Array.from(uniqueSensors).some(function (sensor) {
            const sensorPoints = points.filter(function (point) { return point.sensor === sensor; });
            return sensorPoints.length !== pointCount || sensorPoints.some(function (point) {
                return !Number.isInteger(point.offset) || Math.abs(point.offset) > Math.floor(pointCount / 2);
            });
        })) return null;
        points.sort(function (a, b) { return a.sensor - b.sensor || a.offset - b.offset; });
        return {sequence: sequence, points: points};
    }

    function latestFrame() {
        const state = Number(tracker.parameter('RT_STATE', 0));
        if (state >= 6 && state <= 9) {
            const live = frame('RT_TRACK_', 'RT_TRACK_SEQUENCE', 'RT_TRACK_COMPLETE',
                Number(tracker.parameter('RT_TRACK_POINTS_USED', 0)), Number(tracker.parameter('RT_TRACK_SENSOR_COUNT', 0)));
            if (live) return {name: 'tracking', data: live};
        }
        const diagnostic = frame('RT_DIAG_', 'RT_DIAG_SEQUENCE', 'RT_DIAG_COMPLETE', 5,
            Number(tracker.parameter('RT_DIAG_SENSOR_COUNT', 0)));
        return diagnostic ? {name: 'diagnostics', data: diagnostic} : null;
    }

    function draw(points) {
        const canvas = byId('diagnostics-plot'), context = canvas.getContext('2d');
        const body = byId('diagnostics-table');
        context.clearRect(0, 0, canvas.width, canvas.height);
        body.textContent = '';
        if (!points.length) return;
        const xMin = Math.min.apply(null, points.map(function (p) { return p.real; }));
        const xSpan = Math.max.apply(null, points.map(function (p) { return p.real; })) - xMin || 1;
        const yMin = Math.min.apply(null, points.map(function (p) { return p.imag; }));
        const ySpan = Math.max.apply(null, points.map(function (p) { return p.imag; })) - yMin || 1;
        [1, 2].forEach(function (sensor) {
            context.strokeStyle = sensor === 1 ? '#d5f36a' : '#69d7c6';
            context.fillStyle = context.strokeStyle;
            context.beginPath();
            points.filter(function (p) { return p.sensor === sensor; }).forEach(function (p, index) {
                const x = 20 + (p.real - xMin) / xSpan * (canvas.width - 40);
                const y = canvas.height - 20 - (p.imag - yMin) / ySpan * (canvas.height - 40);
                if (index) context.lineTo(x, y); else context.moveTo(x, y);
                context.fillRect(x - 3, y - 3, 6, 6);
            });
            context.stroke();
        });
        points.forEach(function (p) {
            const row = document.createElement('tr');
            [p.sensor, p.offset, p.frequency, p.real, p.imag].forEach(function (v, column) {
                const cell = document.createElement(column < 2 ? 'th' : 'td');
                cell.textContent = v.toFixed(column < 3 ? 0 : 7);
                row.appendChild(cell);
            });
            body.appendChild(row);
        });
    }

    function update() {
        const state = Number(tracker.parameter('RT_STATE', 0));
        byId('diagnostics-start-button').disabled = state === 2 || state === 4 || state === 5 ||
            (state >= 6 && state <= 9) || !Boolean(tracker.parameter('RT_BASELINE_VALID', false)) ||
            Number(tracker.parameter('RT_BASELINE_ACTIVE_MASK', 0)) !== Number(tracker.parameter('RT_SENSOR_ENABLE_MASK', 1));
        byId('diagnostics-cancel-button').disabled = state !== 5;
        const source = latestFrame(), selected = byId('diagnostics-select-sensor').value;
        const key = (source ? source.name + ':' + source.data.sequence : 'none') + ':' + selected + ':' + tracker.sensorVisibilityVersion;
        if (key === lastKey) return;
        lastKey = key;
        byId('diagnostics-summary').textContent = source ?
            (source.name === 'tracking' ? 'Current complete tracking point frame.' : 'Last complete diagnostic point frame.') :
            'No complete diagnostic point set is available.';
        byId('diagnostics-sequence').textContent = source ? source.data.sequence : '-';
        draw(source ? source.data.points.filter(function (p) {
            return tracker.sensorVisible(p.sensor) && (selected === 'all' || Number(selected) === p.sensor);
        }) : []);
    }

    tracker.registerDashboard('diagnostics', {
        enter: function () {
            if (!bound) {
                bound = true;
                byId('diagnostics-start-button').addEventListener('click', function () { tracker.sendCommand(3); });
                byId('diagnostics-cancel-button').addEventListener('click', function () { tracker.sendCommand(4); });
                byId('diagnostics-select-sensor').addEventListener('change', update);
            }
            update();
        },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
