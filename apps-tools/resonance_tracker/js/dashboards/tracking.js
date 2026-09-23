(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;
    let lastSequence = -1;
    let pendingPoints = null;

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
        }

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
        const backendPoints = Number(tracker.parameter('RT_TRACK_POINTS', 5));
        if (pendingPoints === backendPoints) pendingPoints = null;
        if (pendingPoints === null && document.activeElement !== byId('tracking-points'))
            byId('tracking-points').value = String(backendPoints);
        byId('tracking-points').disabled = active;
        byId('tracking-start-button').disabled = state !== 3 || !baselineValid;
        byId('tracking-stop-button').disabled = !active;
        const recovery = Boolean(tracker.parameter('RT_TRACK_RECOVERY_REQUIRED', false));
        byId('tracking-recovery').hidden = !recovery;
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
