(function (tracker) {
    const button = document.getElementById('tracking-record-button');
    const status = document.getElementById('tracking-record-status');
    const maximumFrames = 10000;
    const fields = ['FREQUENCY_HZ', 'Q', 'SE_HZ', 'NORMALIZED_RESIDUAL', 'TEMPLATE_GAIN',
        'REQUESTED_SHIFT_HZ', 'APPLIED_SHIFT_HZ', 'LOSS_COUNTER', 'FIT_VALID', 'SENSOR_STATE'];
    const pointFields = ['SENSOR_ID', 'OFFSET', 'FREQUENCY_HZ', 'RE', 'IM'];
    const header = ['received_utc', 'elapsed_s', 'baseline_sequence', 'track_sequence',
        'instrument_state', 'sensor_id'].concat(fields.map(function (field) {
            return field.toLowerCase();
        }), ['points_used', 'point_offsets', 'point_frequency_hz', 'point_re', 'point_im']);
    let recording = false;
    let rows = [];
    let frames = 0;
    let lastKey = null;
    let startedAt = 0;
    let filename = '';

    function values(name) {
        const signal = tracker.store.signals[name];
        return signal && Array.isArray(signal.value) ? signal.value : [];
    }

    function frameKey() {
        return tracker.parameter('RT_BASELINE_SEQUENCE', 0) + ':' + tracker.parameter('RT_TRACK_SEQUENCE', 0);
    }

    function updateButton() {
        const state = Number(tracker.parameter('RT_STATE', 0));
        button.disabled = !recording && !(state >= 6 && state <= 9);
        button.textContent = recording ? 'STOP REC' : 'REC';
        button.classList.toggle('stop', recording);
        button.setAttribute('aria-pressed', String(recording));
    }

    function csvCell(value) {
        return '"' + String(value).replace(/"/g, '""') + '"';
    }

    function stop(reason) {
        if (!recording) return;
        recording = false;
        updateButton();
        if (frames) {
            const blob = new Blob([header.join(',') + '\r\n', rows.join('\r\n') + '\r\n'],
                {type: 'text/csv;charset=utf-8'});
            const url = URL.createObjectURL(blob);
            const link = document.createElement('a');
            link.href = url;
            link.download = filename;
            document.body.appendChild(link);
            link.click();
            link.remove();
            window.setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
            status.textContent = reason + ' Downloaded ' + frames + ' frames to ' + filename + '.';
        } else {
            status.textContent = reason + ' No new complete tracking frames received.';
        }
        rows = [];
    }

    function observe() {
        updateButton();
        if (!recording) return;
        const key = frameKey();
        if (key === lastKey) return;
        const sequence = Number(tracker.parameter('RT_TRACK_SEQUENCE', 0));
        const signalSequence = values('RT_TRACK_SIGNAL_SEQUENCE');
        const sensorCount = Number(tracker.parameter('RT_TRACK_SENSOR_COUNT', 0));
        const pointCount = Number(tracker.parameter('RT_TRACK_POINTS_USED', 0));
        const sensors = values('RT_TRACK_SENSOR_ID');
        const data = fields.map(function (field) { return values('RT_TRACK_' + field); });
        const points = pointFields.map(function (field) { return values('RT_TRACK_POINT_' + field); });
        // Parameters and signals may arrive separately: wait for the matching complete frame.
        if (!tracker.parameter('RT_TRACK_COMPLETE', false) || sequence <= 0 ||
            signalSequence.length !== 1 || Number(signalSequence[0]) !== sequence ||
            (sensorCount !== 1 && sensorCount !== 2) || (pointCount !== 3 && pointCount !== 5) ||
            sensors.length !== sensorCount || data.some(function (array) { return array.length !== sensorCount; }) ||
            points.some(function (array) { return array.length !== sensorCount * pointCount; })) return;
        if (sensors.some(function (id, index) {
            return (Number(id) !== 1 && Number(id) !== 2) || sensors.indexOf(id) !== index;
        }) || data.concat(points).some(function (array) {
            return array.some(function (value) { return !Number.isFinite(Number(value)); });
        })) return;
        const sensorPoints = sensors.map(function (id) {
            const indices = [];
            points[0].forEach(function (sensor, index) {
                if (Number(sensor) === Number(id)) indices.push(index);
            });
            return indices.sort(function (left, right) { return points[1][left] - points[1][right]; });
        });
        if (sensorPoints.some(function (indices) {
            return indices.length !== pointCount || indices.some(function (index, offset) {
                return Number(points[1][index]) !== offset - Math.floor(pointCount / 2);
            });
        })) return;
        const timestamp = new Date().toISOString();
        const elapsed = (performance.now() - startedAt) / 1000;
        sensors.forEach(function (id, index) {
            const row = [timestamp, elapsed, tracker.parameter('RT_BASELINE_SEQUENCE', 0), sequence,
                tracker.stateName(Number(tracker.parameter('RT_STATE', 0))), id];
            data.forEach(function (array) { row.push(array[index]); });
            row.push(pointCount);
            points.slice(1).forEach(function (array) {
                row.push(JSON.stringify(sensorPoints[index].map(function (point) { return Number(array[point]); })));
            });
            rows.push(row.map(csvCell).join(','));
        });
        lastKey = key;
        ++frames;
        status.textContent = 'Recording: ' + frames + ' / ' + maximumFrames + ' received frames. Stop Rec downloads CSV.';
        if (frames >= maximumFrames) stop('Recording limit reached.');
    }

    button.addEventListener('click', function () {
        if (recording) { stop('Recording stopped.'); return; }
        rows = [];
        frames = 0;
        lastKey = frameKey();
        startedAt = performance.now();
        filename = 'resonance_tracking_' + new Date().toISOString().replace(/[:.]/g, '-') + '.csv';
        recording = true;
        status.textContent = 'Recording: waiting for a new complete tracking frame.';
        updateButton();
    });

    tracker.trackingRecorder = {observe: observe, stop: stop};
}(window.ResonanceTracker));
