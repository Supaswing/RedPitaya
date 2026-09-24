(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;
    let lastSequence = -1;
    let lastVisibilityVersion = -1;
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
        ['tracking-histogram', 'tracking-quality-plot', 'tracking-complex-plot'].forEach(function (id) {
            const plot = byId(id);
            plot.getContext('2d').clearRect(0, 0, plot.width, plot.height);
        });
        byId('tracking-stat-1').textContent = '-';
        byId('tracking-stat-2').textContent = '-';
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
            if (!item.points.length || !tracker.sensorVisible(item.sensor)) return;
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

    function drawStatistics() {
        const histogram = byId('tracking-histogram');
        const histogramContext = histogram.getContext('2d');
        const quality = byId('tracking-quality-plot');
        const qualityContext = quality.getContext('2d');
        histogramContext.clearRect(0, 0, histogram.width, histogram.height);
        qualityContext.clearRect(0, 0, quality.width, quality.height);
        const visible = [1, 2].filter(tracker.sensorVisible);
        const frequencies = [];
        let residualMax = 0.1;
        visible.forEach(function (sensor) {
            const history = frequencyHistory[sensor];
            if (!history.length) return;
            const mean = history.reduce(function (sum, p) { return sum + p.frequency; }, 0) / history.length;
            const variance = history.reduce(function (sum, p) { return sum + Math.pow(p.frequency - mean, 2); }, 0) /
                Math.max(1, history.length - 1);
            byId('tracking-stat-' + sensor).textContent = mean.toFixed(1) + ' / ' + Math.sqrt(variance).toFixed(1);
            history.forEach(function (p) {
                frequencies.push(p.frequency);
                if (Number.isFinite(p.residual)) residualMax = Math.max(residualMax, p.residual);
            });
        });
        [1, 2].filter(function (sensor) { return !visible.includes(sensor); }).forEach(function (sensor) {
            byId('tracking-stat-' + sensor).textContent = '-';
        });
        if (!frequencies.length) return;
        const minimum = Math.min.apply(null, frequencies), maximum = Math.max.apply(null, frequencies);
        const span = Math.max(1, maximum - minimum), bins = 16;
        visible.forEach(function (sensor) {
            const history = frequencyHistory[sensor], counts = Array(bins).fill(0);
            history.forEach(function (p) {
                const bin = Math.min(bins - 1, Math.floor((p.frequency - minimum) / span * bins));
                counts[bin] += 1;
            });
            const maxCount = Math.max.apply(null, counts.concat(1));
            histogramContext.fillStyle = sensor === 1 ? '#d5f36a' : '#69d7c6';
            counts.forEach(function (count, bin) {
                const x = 36 + bin * (histogram.width - 60) / bins;
                const width = (histogram.width - 60) / bins;
                const height = count / maxCount * (histogram.height - 46);
                histogramContext.fillRect(x + (sensor === 2 ? width / 2 : 0), histogram.height - 26 - height,
                    width / 2 - 1, height);
            });
            qualityContext.strokeStyle = sensor === 1 ? '#d5f36a' : '#69d7c6';
            qualityContext.beginPath();
            history.forEach(function (p, index) {
                const x = 36 + index / Math.max(1, history.length - 1) * (quality.width - 60);
                const y = quality.height - 25 - Math.min(residualMax, Math.max(0, p.residual || 0)) /
                    residualMax * (quality.height - 45);
                if (index) qualityContext.lineTo(x, y); else qualityContext.moveTo(x, y);
            });
            qualityContext.stroke();
        });
        histogramContext.fillStyle = qualityContext.fillStyle = '#8d9d98';
        histogramContext.font = qualityContext.font = '11px monospace';
        histogramContext.fillText(minimum.toFixed(1) + ' Hz', 36, histogram.height - 8);
        histogramContext.fillText(maximum.toFixed(1) + ' Hz', histogram.width - 130, histogram.height - 8);
        qualityContext.fillText('0', 8, quality.height - 25);
        qualityContext.fillText(residualMax.toFixed(3), 3, 20);
    }

    function drawComplex(sensorId, real, imag, count) {
        const canvas = byId('tracking-complex-plot');
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, canvas.width, canvas.height);
        const groups = {1: [], 2: []};
        for (let index = 0; index < count; ++index) {
            const sensor = Number(sensorId[index]);
            const re = Number(real[index]), im = Number(imag[index]);
            if (groups[sensor] && tracker.sensorVisible(sensor) && Number.isFinite(re) && Number.isFinite(im))
                groups[sensor].push([re, im]);
        }
        const all = groups[1].concat(groups[2]);
        if (!all.length) return;
        const reMin = Math.min.apply(null, all.map(function (p) { return p[0]; }));
        const reMax = Math.max.apply(null, all.map(function (p) { return p[0]; }));
        const imMin = Math.min.apply(null, all.map(function (p) { return p[1]; }));
        const imMax = Math.max.apply(null, all.map(function (p) { return p[1]; }));
        const reSpan = reMax - reMin || 1, imSpan = imMax - imMin || 1;
        [1, 2].forEach(function (sensor) {
            context.strokeStyle = sensor === 1 ? '#d5f36a' : '#69d7c6';
            context.fillStyle = context.strokeStyle;
            context.beginPath();
            groups[sensor].forEach(function (point, index) {
                const x = 30 + (point[0] - reMin) / reSpan * (canvas.width - 60);
                const y = canvas.height - 30 - (point[1] - imMin) / imSpan * (canvas.height - 60);
                if (index) context.lineTo(x, y); else context.moveTo(x, y);
                context.fillRect(x - 3, y - 3, 6, 6);
            });
            context.stroke();
        });
    }

    function renderFrame(appendHistory) {
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
        const sensorState = values('RT_TRACK_SENSOR_STATE');
        const arrays = [frequency, q, se, residual, gain, requestedShift, appliedShift, loss, fitValid, sensorState];
        const coherent = Boolean(tracker.parameter('RT_TRACK_COMPLETE', false)) && sequence > 0 &&
            signalSequence.length === 1 && Number(signalSequence[0]) === sequence &&
            sensorCount > 0 && sensorId.length >= sensorCount &&
            arrays.every(function (array) { return array.length >= sensorCount; });
        if (!coherent) return false;

        clearSensor(1);
        clearSensor(2);
        byId('tracking-sensor-1').hidden = !tracker.sensorVisible(1) || !sensorId.slice(0, sensorCount).some(function (id) { return Number(id) === 1; });
        byId('tracking-sensor-2').hidden = !tracker.sensorVisible(2) || !sensorId.slice(0, sensorCount).some(function (id) { return Number(id) === 2; });
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
            byId('tracking-fit-' + sensor).textContent =
                ({1: 'TRACKING', 2: 'DEGRADED', 3: 'LOST'})[Number(sensorState[index])] || 'UNKNOWN';
            const sensorFrequency = Number(frequency[index]);
            if (appendHistory && Number.isFinite(sensorFrequency)) {
                frequencyHistory[sensor].push({sequence: sequence, frequency: sensorFrequency,
                    residual: Number(residual[index])});
                if (frequencyHistory[sensor].length > maximumHistory) frequencyHistory[sensor].shift();
            }
        }
        drawFrequencyHistory();
        drawStatistics();

        const pointSensor = values('RT_TRACK_POINT_SENSOR_ID');
        const pointOffset = values('RT_TRACK_POINT_OFFSET');
        const pointFrequency = values('RT_TRACK_POINT_FREQUENCY_HZ');
        const pointReal = values('RT_TRACK_POINT_RE');
        const pointImag = values('RT_TRACK_POINT_IM');
        const expectedPoints = sensorCount * pointCount;
        const body = byId('tracking-point-table');
        body.textContent = '';
        if ([pointSensor, pointOffset, pointFrequency, pointReal, pointImag].every(function (array) {
            return array.length === expectedPoints;
        })) {
            drawComplex(pointSensor, pointReal, pointImag, expectedPoints);
            const rows = [];
            for (let index = 0; index < expectedPoints; ++index) rows.push(index);
            rows.sort(function (left, right) {
                return Number(pointSensor[left]) - Number(pointSensor[right]) ||
                    Number(pointOffset[left]) - Number(pointOffset[right]);
            });
            rows.filter(function (index) { return tracker.sensorVisible(Number(pointSensor[index])); }).forEach(function (index) {
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
        byId('tracking-start-button').disabled = state !== 3 || !baselineValid ||
            Number(tracker.parameter('RT_BASELINE_ACTIVE_MASK', 0)) !== Number(tracker.parameter('RT_SENSOR_ENABLE_MASK', 1));
        byId('tracking-stop-button').disabled = !active;
        const recovery = Boolean(tracker.parameter('RT_TRACK_RECOVERY_REQUIRED', false)) ||
            Boolean(tracker.parameter('RT_RELOCK_ACTIVE', false)) ||
            Boolean(tracker.parameter('RT_RELOCK_FALLBACK', false));
        byId('tracking-recovery').hidden = !recovery;
        if (recovery) {
            const activeRelock = Boolean(tracker.parameter('RT_RELOCK_ACTIVE', false));
            byId('tracking-recovery').textContent = activeRelock ?
                'Local re-lock: sensor ' + tracker.parameter('RT_RELOCK_SENSOR_ID', '-') +
                ', attempt ' + tracker.parameter('RT_RELOCK_ATTEMPT', '-') +
                ', ' + Number(tracker.parameter('RT_RELOCK_PROGRESS', 0)).toFixed(1) + '% of scan budget.' :
                String(tracker.parameter('RT_RELOCK_REASON', '') || 'Recovery is starting.');
        }
        const relockReason = String(tracker.parameter('RT_RELOCK_REASON', '') || '');
        byId('tracking-relock-status').hidden = !relockReason;
        byId('tracking-relock-status').textContent = relockReason;
        setNumber('tracking-rate', tracker.parameter('RT_TRACK_RATE_HZ', 0), 2);
        byId('tracking-summary').textContent = complete ?
            (state === 8 ? 'Tracking is degraded; poor frames do not move the center.' :
                'Displaying the latest complete coherent tracking frame.') :
            (baselineValid ? 'Ready to track from the last completed baseline.' :
                'Acquire a valid baseline before starting tracking.');
        const sequence = Number(tracker.parameter('RT_TRACK_SEQUENCE', 0));
        if (sequence !== lastSequence || lastVisibilityVersion !== tracker.sensorVisibilityVersion) {
            if (renderFrame(sequence !== lastSequence)) {
                lastSequence = sequence;
                lastVisibilityVersion = tracker.sensorVisibilityVersion;
            }
        }
    }

    tracker.registerDashboard('tracking', {
        enter: function () { bindControls(); update(); },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
