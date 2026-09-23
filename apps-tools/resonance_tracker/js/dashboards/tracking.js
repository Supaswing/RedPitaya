(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;
    let lastSequence = -1;
    let lastBaselineSequence = null;
    let pendingPoints = null;
    const maximumHistory = 180;
    const frequencyHistory = {1: [], 2: []};

    function values(name) {
        const signal = tracker.store.signals[name];
        return signal && Array.isArray(signal.value) ? signal.value : [];
    }

    function setNumber(id, value, digits) {
        const number = Number(value);
        byId(id).textContent = Number.isFinite(number) ? number.toFixed(digits) : '-';
    }

    function clearSensor(sensor) {
        ['frequency', 'q', 'se', 'residual', 'gain', 'requested-shift', 'applied-shift', 'loss'].forEach(function (name) {
            byId('tracking-' + name + '-' + sensor).textContent = '-';
        });
        byId('tracking-fit-' + sensor).textContent = 'NO FRAME';
    }

    function resetFrequencyHistory() {
        frequencyHistory[1] = [];
        frequencyHistory[2] = [];
        const canvas = byId('tracking-frequency-plot');
        canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height);
    }

    function drawFrequencyHistory() {
        const canvas = byId('tracking-frequency-plot');
        const context = canvas.getContext('2d');
        const left = 64, right = canvas.width - 16, top = 20, bottom = canvas.height - 30;
        context.clearRect(0, 0, canvas.width, canvas.height);
        const all = frequencyHistory[1].concat(frequencyHistory[2]);
        if (!all.length) return;
        const firstSequence = Math.min.apply(null, all.map(function (point) { return point.sequence; }));
        const lastHistorySequence = Math.max.apply(null, all.map(function (point) { return point.sequence; }));
        const sequenceSpan = Math.max(1, lastHistorySequence - firstSequence);
        const series = [1, 2].map(function (sensor) {
            const points = frequencyHistory[sensor];
            if (!points.length) return {sensor: sensor, points: []};
            const reference = points[0].frequency;
            return {sensor: sensor, reference: reference, points: points.map(function (point) {
                return {sequence: point.sequence, delta: point.frequency - reference};
            })};
        });
        const deltas = [0];
        series.forEach(function (item) { item.points.forEach(function (point) { deltas.push(point.delta); }); });
        let minimum = Math.min.apply(null, deltas), maximum = Math.max.apply(null, deltas);
        const padding = Math.max(1, 0.1 * (maximum - minimum));
        minimum -= padding;
        maximum += padding;
        const ySpan = maximum - minimum;
        const x = function (sequence) { return left + (sequence - firstSequence) / sequenceSpan * (right - left); };
        const y = function (delta) { return bottom - (delta - minimum) / ySpan * (bottom - top); };

        context.strokeStyle = '#53615e';
        context.lineWidth = 1;
        context.beginPath();
        context.moveTo(left, top); context.lineTo(left, bottom); context.lineTo(right, bottom);
        context.stroke();
        if (minimum <= 0 && maximum >= 0) {
            context.setLineDash([4, 4]);
            context.beginPath(); context.moveTo(left, y(0)); context.lineTo(right, y(0)); context.stroke();
            context.setLineDash([]);
        }
        context.fillStyle = '#8d9d98';
        context.font = '11px monospace';
        context.fillText(maximum.toFixed(1) + ' Hz', 4, top + 4);
        context.fillText(minimum.toFixed(1) + ' Hz', 4, bottom);
        context.fillText(String(firstSequence), left, canvas.height - 8);
        context.textAlign = 'right';
        context.fillText(String(lastHistorySequence), right, canvas.height - 8);
        context.textAlign = 'left';
        series.forEach(function (item) {
            if (!item.points.length) return;
            context.strokeStyle = item.sensor === 1 ? '#d5f36a' : '#69d7c6';
            context.lineWidth = 2;
            context.beginPath();
            item.points.forEach(function (point, index) {
                if (index) context.lineTo(x(point.sequence), y(point.delta));
                else context.moveTo(x(point.sequence), y(point.delta));
            });
            context.stroke();
            context.fillStyle = context.strokeStyle;
            const latest = item.points[item.points.length - 1];
            context.fillRect(x(latest.sequence) - 2, y(latest.delta) - 2, 4, 4);
            context.fillText('S' + item.sensor + ' base ' + item.reference.toFixed(1) + ' Hz',
                left + 8, top + 14 * item.sensor);
        });
    }

    function renderFrame() {
        const sequence = Number(tracker.parameter('RT_TRACK_SEQUENCE', 0));
        const signalSequence = values('RT_TRACK_SIGNAL_SEQUENCE');
        const sensorCount = Number(tracker.parameter('RT_TRACK_SENSOR_COUNT', 0));
        const pointCount = Number(tracker.parameter('RT_TRACK_POINTS_USED', 0));
        const sensorId = values('RT_TRACK_SENSOR_ID');
        const frequency = values('RT_TRACK_FREQUENCY_HZ');
        const q = values('RT_TRACK_Q');
        const se = values('RT_TRACK_SE_HZ');
        const residual = values('RT_TRACK_NORMALIZED_RESIDUAL');
        const gain = values('RT_TRACK_TEMPLATE_GAIN');
        const requestedShift = values('RT_TRACK_REQUESTED_SHIFT_HZ');
        const appliedShift = values('RT_TRACK_APPLIED_SHIFT_HZ');
        const loss = values('RT_TRACK_LOSS_COUNTER');
        const fitValid = values('RT_TRACK_FIT_VALID');
        const arrays = [frequency, q, se, residual, gain, requestedShift, appliedShift, loss, fitValid];
        const coherent = Boolean(tracker.parameter('RT_TRACK_COMPLETE', false)) && sequence > 0 &&
            signalSequence.length === 1 && Number(signalSequence[0]) === sequence &&
            sensorCount > 0 && sensorId.length >= sensorCount &&
            arrays.every(function (array) { return array.length >= sensorCount; });
        if (!coherent) return false;

        clearSensor(1);
        clearSensor(2);
        byId('tracking-sensor-2').hidden = sensorCount < 2;
        for (let index = 0; index < sensorCount; ++index) {
            const sensor = Number(sensorId[index]);
            if (sensor !== 1 && sensor !== 2) continue;
            setNumber('tracking-frequency-' + sensor, frequency[index], 1);
            setNumber('tracking-q-' + sensor, q[index], 3);
            setNumber('tracking-se-' + sensor, se[index], 1);
            setNumber('tracking-residual-' + sensor, residual[index], 5);
            setNumber('tracking-gain-' + sensor, gain[index], 3);
            setNumber('tracking-requested-shift-' + sensor, requestedShift[index], 1);
            setNumber('tracking-applied-shift-' + sensor, appliedShift[index], 1);
            setNumber('tracking-loss-' + sensor, loss[index], 0);
            const good = Number(fitValid[index]) === 1 && Number(loss[index]) === 0;
            byId('tracking-fit-' + sensor).textContent = good ? 'GOOD' : 'POOR';
            const sensorFrequency = Number(frequency[index]);
            if (Number.isFinite(sensorFrequency)) {
                frequencyHistory[sensor].push({sequence: sequence, frequency: sensorFrequency});
                if (frequencyHistory[sensor].length > maximumHistory) frequencyHistory[sensor].shift();
            }
        }
        drawFrequencyHistory();

        const pointSensor = values('RT_TRACK_POINT_SENSOR_ID');
        const pointOffset = values('RT_TRACK_POINT_OFFSET');
        const pointFrequency = values('RT_TRACK_POINT_FREQUENCY_HZ');
        const pointReal = values('RT_TRACK_POINT_RE');
        const pointImag = values('RT_TRACK_POINT_IM');
        const expectedPoints = sensorCount * pointCount;
        const body = byId('tracking-point-table');
        body.textContent = '';
        if ([pointSensor, pointOffset, pointFrequency, pointReal, pointImag].every(function (array) {
            return array.length >= expectedPoints;
        })) {
            const rows = [];
            for (let index = 0; index < expectedPoints; ++index) rows.push(index);
            rows.sort(function (left, right) {
                return Number(pointSensor[left]) - Number(pointSensor[right]) ||
                    Number(pointOffset[left]) - Number(pointOffset[right]);
            });
            rows.forEach(function (index) {
                const row = document.createElement('tr');
                [pointSensor[index], pointOffset[index], pointFrequency[index], pointReal[index], pointImag[index]].forEach(function (value, column) {
                    const cell = document.createElement(column < 2 ? 'th' : 'td');
                    cell.textContent = Number(value).toFixed(column < 3 ? 0 : 7);
                    row.appendChild(cell);
                });
                body.appendChild(row);
            });
        }
        byId('tracking-sequence').textContent = sequence;
        byId('tracking-points-used').textContent = pointCount;
        return true;
    }

    function bindControls() {
        if (controlsBound) return;
        controlsBound = true;
        byId('tracking-start-button').addEventListener('click', function () {
            const points = Number(byId('tracking-points').value);
            pendingPoints = points;
            if (!tracker.sendCommand(5, {RT_TRACK_POINTS: {value: points}})) pendingPoints = null;
        });
        byId('tracking-stop-button').addEventListener('click', function () { tracker.sendCommand(6); });
        byId('tracking-points').addEventListener('change', function () {
            const points = Number(byId('tracking-points').value);
            pendingPoints = points;
            if (!tracker.transport.send({RT_TRACK_POINTS: {value: points}})) pendingPoints = null;
        });
    }

    function update() {
        const state = Number(tracker.parameter('RT_STATE', 0));
        const active = state >= 6 && state <= 9;
        const baselineValid = Boolean(tracker.parameter('RT_BASELINE_VALID', false));
        const complete = Boolean(tracker.parameter('RT_TRACK_COMPLETE', false));
        const baselineSequence = Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0));
        if (lastBaselineSequence !== baselineSequence) {
            resetFrequencyHistory();
            lastSequence = -1;
            lastBaselineSequence = baselineSequence;
        }
        const backendPoints = Number(tracker.parameter('RT_TRACK_POINTS', 5));
        if (pendingPoints === backendPoints) pendingPoints = null;
        if (pendingPoints === null && document.activeElement !== byId('tracking-points'))
            byId('tracking-points').value = String(backendPoints);
        byId('tracking-points').disabled = active;
        byId('tracking-start-button').disabled = state !== 3 || !baselineValid;
        byId('tracking-stop-button').disabled = !active;
        const recovery = Boolean(tracker.parameter('RT_TRACK_RECOVERY_REQUIRED', false));
        byId('tracking-recovery').hidden = !recovery;
        setNumber('tracking-rate', tracker.parameter('RT_TRACK_RATE_HZ', 0), 2);
        byId('tracking-summary').textContent = complete ?
            (state === 8 ? 'Tracking is degraded; poor frames do not move the center.' :
                'Displaying the latest complete coherent tracking frame.') :
            (baselineValid ? 'Ready to track from the last completed baseline.' :
                'Acquire a valid baseline before starting tracking.');
        const sequence = Number(tracker.parameter('RT_TRACK_SEQUENCE', 0));
        if (sequence !== lastSequence && renderFrame()) lastSequence = sequence;
    }

    tracker.registerDashboard('tracking', {
        enter: function () { bindControls(); update(); },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
