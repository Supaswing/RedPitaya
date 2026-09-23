(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;
    const storageKey = 'resonance-tracker-tuning-baselines-v1';
    const maxBaselines = 24;
    const colors = ['#d5f36a', '#69d7c6', '#9d8cff', '#f0a35e', '#f58a75', '#82b9fa'];
    let baselines = [];
    let nextIteration = 1;
    let lastCapturedSequence = null;
    let pendingIteration = null;
    let tableSignature = '';

    function loadBaselines() {
        try {
            const saved = JSON.parse(window.localStorage.getItem(storageKey) || '[]');
            if (Array.isArray(saved)) baselines = saved.filter(function (row) {
                return row && Number.isInteger(row.iteration) && row.iteration > 0 &&
                    (row.status !== 'Complete' || row.curve && Array.isArray(row.curve.frequency) &&
                     Array.isArray(row.curve.magnitude) && row.curve.frequency.length === row.curve.magnitude.length);
            }).slice(-maxBaselines);
        } catch (error) { baselines = []; }
        nextIteration = baselines.reduce(function (value, row) { return Math.max(value, row.iteration + 1); }, 1);
    }

    function saveBaselines() {
        try { window.localStorage.setItem(storageKey, JSON.stringify(baselines)); }
        catch (error) { byId('tuning-baseline-status').textContent = 'Browser storage is full; this session still has the curves.'; }
    }

    function format(value, digits) { return Number.isFinite(value) ? value.toFixed(digits) : '-'; }

    function renderTable() {
        const signature = baselines.map(function (row) {
            return [row.iteration, row.status, row.checked, row.fres].join(':');
        }).join('|');
        if (signature === tableSignature) return;
        tableSignature = signature;
        const body = byId('tuning-baseline-table');
        body.textContent = '';
        baselines.slice().reverse().forEach(function (row) {
            const tr = document.createElement('tr');
            tr.dataset.selected = String(Boolean(row.checked));
            const show = document.createElement('td');
            if (row.status === 'Complete') {
                const checkbox = document.createElement('input');
                checkbox.type = 'checkbox';
                checkbox.checked = Boolean(row.checked);
                checkbox.setAttribute('aria-label', 'Show baseline iteration ' + row.iteration);
                checkbox.addEventListener('change', function () {
                    row.checked = checkbox.checked;
                    saveBaselines();
                    update();
                });
                show.appendChild(checkbox);
            }
            tr.appendChild(show);
            [row.iteration, row.status === 'Complete' ? format(row.fres, 1) : row.status,
                format(row.q, 2), format(row.fwhm, 1), row.fit || '-',
                format(row.noise, 6), format(row.quality, 3),
                Number.isFinite(row.slope) ? row.slope.toExponential(3) : '-', format(row.se, 1)]
                .forEach(function (value) {
                    const cell = document.createElement('td');
                    cell.textContent = String(value);
                    tr.appendChild(cell);
                });
            body.appendChild(tr);
        });
        if (!baselines.length) byId('tuning-baseline-status').textContent = 'No saved baselines in this browser.';
        else byId('tuning-baseline-status').textContent = baselines.length + ' iterations saved locally; newest 24 retained.';
    }

    function values(name) {
        const signal = tracker.store.signals[name];
        return signal && Array.isArray(signal.value) ? signal.value : [];
    }

    function finiteParameter(name) {
        const value = Number(tracker.parameter(name, NaN));
        return Number.isFinite(value) ? value : NaN;
    }

    function setNumber(id, value, digits, suffix) {
        byId(id).textContent = Number.isFinite(value) ? value.toFixed(digits) + (suffix || '') : '-';
    }

    function loadSetting(id, fallback) {
        let value = fallback;
        try {
            const saved = window.localStorage.getItem('resonance-tracker-' + id);
            if (saved !== null && Number.isFinite(Number(saved))) value = Number(saved);
        } catch (error) {
            // Browser storage is optional; the input value remains session-local.
        }
        byId(id).value = value;
    }

    function saveSetting(id) {
        try { window.localStorage.setItem('resonance-tracker-' + id, byId(id).value); } catch (error) {}
    }

    function bindControls() {
        if (controlsBound) return;
        controlsBound = true;
        loadBaselines();
        baselines.forEach(function (row) { if (row.status === 'Acquiring') row.status = 'Interrupted'; });
        saveBaselines();
        renderTable();
        loadSetting('tuning-target-frequency', 30000000);
        loadSetting('tuning-target-separation', 0);
        loadSetting('tuning-tolerance', 1000);
        ['tuning-target-frequency','tuning-target-separation','tuning-tolerance'].forEach(function (id) {
            byId(id).addEventListener('change', function () {
                saveSetting(id);
                update();
            });
        });
        byId('tuning-acquire-button').addEventListener('click', function () {
            if (!tracker.sendCommand(1)) return;
            const row = {iteration: nextIteration++, status: 'Acquiring', checked: true,
                afterSequence: Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0))};
            baselines.push(row);
            if (baselines.length > maxBaselines) baselines.shift();
            pendingIteration = row.iteration;
            saveBaselines();
            update();
        });
        byId('tuning-cancel-button').addEventListener('click', function () {
            if (!tracker.sendCommand(2)) return;
            const row = baselines.find(function (candidate) { return candidate.iteration === pendingIteration; });
            if (row) {
                row.status = 'Cancelled';
                pendingIteration = null;
                saveBaselines();
                update();
            }
        });
        captureBaseline();
    }

    function resonanceEstimates() {
        const trackingSequence = Number(tracker.parameter('RT_TRACK_SEQUENCE', 0));
        const trackingSignalSequence = values('RT_TRACK_SIGNAL_SEQUENCE');
        const trackingCount = Number(tracker.parameter('RT_TRACK_SENSOR_COUNT', 0));
        const trackingSensor = values('RT_TRACK_SENSOR_ID');
        const trackingFrequency = values('RT_TRACK_FREQUENCY_HZ');
        if (Boolean(tracker.parameter('RT_TRACK_COMPLETE', false)) && trackingSequence > 0 &&
            trackingSignalSequence.length === 1 && Number(trackingSignalSequence[0]) === trackingSequence &&
            trackingCount > 0 && trackingSensor.length >= trackingCount && trackingFrequency.length >= trackingCount) {
            const result = {source: 'TRACKING', detail: 'sequence ' + trackingSequence, frequencies: {}};
            for (let index = 0; index < trackingCount; ++index) {
                const sensor = Number(trackingSensor[index]);
                const frequency = Number(trackingFrequency[index]);
                if ((sensor === 1 || sensor === 2) && Number.isFinite(frequency)) result.frequencies[sensor] = frequency;
            }
            return result;
        }

        const baselineCount = Number(tracker.parameter('RT_RESONANCE_COUNT', 0));
        const fitFrequency = values('RT_FIT_FREQUENCY_HZ');
        if (Boolean(tracker.parameter('RT_BASELINE_VALID', false)) && baselineCount > 0 &&
            fitFrequency.length >= baselineCount) {
            const result = {
                source: 'BASELINE',
                detail: 'sequence ' + Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0)),
                frequencies: {}
            };
            for (let index = 0; index < Math.min(2, baselineCount); ++index) {
                const frequency = Number(fitFrequency[index]);
                if (Number.isFinite(frequency)) result.frequencies[index + 1] = frequency;
            }
            return result;
        }
        return {source: 'NONE', detail: 'Acquire a valid baseline', frequencies: {}};
    }

    function renderError(id, frequency, target, tolerance, quantity) {
        const element = byId(id);
        const label = quantity || 'RESONANCE';
        element.className = '';
        if (!Number.isFinite(frequency) || !Number.isFinite(target)) {
            element.textContent = 'No valid target comparison';
            return;
        }
        const error = frequency - target;
        element.className = Math.abs(error) <= tolerance ? 'good' : 'adjust';
        element.textContent = (error >= 0 ? '+' : '') + error.toFixed(1) + ' Hz / ' +
            (Math.abs(error) <= tolerance ? 'IN TOLERANCE' : (error < 0 ? label + ' LOW' : label + ' HIGH'));
    }

    function relativeNoise(meanI, meanQ, standardDeviationI, standardDeviationQ) {
        const magnitude = Math.hypot(meanI, meanQ);
        return magnitude > 0 ? 100 * Math.hypot(standardDeviationI, standardDeviationQ) / magnitude : NaN;
    }

    function trackedReflectionMagnitude() {
        const sequence = Number(tracker.parameter('RT_TRACK_SEQUENCE', 0));
        const signalSequence = values('RT_TRACK_SIGNAL_SEQUENCE');
        if (!Boolean(tracker.parameter('RT_TRACK_COMPLETE', false)) || sequence <= 0 ||
            signalSequence.length !== 1 || Number(signalSequence[0]) !== sequence) return NaN;
        const sensor = values('RT_TRACK_POINT_SENSOR_ID');
        const offset = values('RT_TRACK_POINT_OFFSET');
        const real = values('RT_TRACK_POINT_RE');
        const imag = values('RT_TRACK_POINT_IM');
        let selected = -1;
        for (let index = 0; index < sensor.length && index < offset.length &&
             index < real.length && index < imag.length; ++index) {
            if (Number(sensor[index]) !== 1) continue;
            if (selected < 0 || Math.abs(Number(offset[index])) < Math.abs(Number(offset[selected]))) selected = index;
        }
        return selected >= 0 ? Math.hypot(Number(real[selected]), Number(imag[selected])) : NaN;
    }

    function baselineData() {
        const sequence = Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0));
        const signalSequence = values('RT_BASELINE_SIGNAL_SEQUENCE');
        const frequency = values('RT_BASELINE_FREQUENCY').map(Number);
        const real = values('RT_BASELINE_RE').map(Number);
        const imag = values('RT_BASELINE_IM').map(Number);
        const coherent = Boolean(tracker.parameter('RT_BASELINE_COMPLETE', false)) && sequence > 0 &&
            signalSequence.length === 1 && Number(signalSequence[0]) === sequence && frequency.length >= 2 &&
            real.length === frequency.length && imag.length === frequency.length;
        if (!coherent) return null;
        const magnitude = real.map(function (value, index) { return Math.hypot(value, imag[index]); });
        const refineSensor = values('RT_REFINE_SENSOR_ID');
        const refineFrequency = values('RT_REFINE_FREQUENCY_HZ');
        const refineReal = values('RT_REFINE_RE');
        const refineImag = values('RT_REFINE_IM');
        let minimum = null;
        for (let index = 0; index < refineSensor.length && index < refineFrequency.length &&
             index < refineReal.length && index < refineImag.length; ++index) {
            if (Number(refineSensor[index]) !== 1) continue;
            const point = {
                frequency: Number(refineFrequency[index]),
                magnitude: Math.hypot(Number(refineReal[index]), Number(refineImag[index])),
                scope: 'sensor 1 refinement'
            };
            if (Number.isFinite(point.frequency) && Number.isFinite(point.magnitude) &&
                (minimum === null || point.magnitude < minimum.magnitude)) minimum = point;
        }
        if (minimum === null) {
            magnitude.forEach(function (value, index) {
                if (!Number.isFinite(value)) return;
                if (minimum === null || value < minimum.magnitude)
                    minimum = {frequency: frequency[index], magnitude: value, scope: 'overview fallback'};
            });
        }
        return {
            frequency: frequency,
            magnitude: magnitude,
            minimum: minimum
        };
    }

    function captureBaseline() {
        const sequence = Number(tracker.parameter('RT_BASELINE_SEQUENCE', 0));
        const complete = Boolean(tracker.parameter('RT_BASELINE_COMPLETE', false));
        const valid = Boolean(tracker.parameter('RT_BASELINE_VALID', false));
        if (complete && valid && sequence > 0 && sequence !== lastCapturedSequence) {
            const curve = baselineData();
            if (curve && Number(tracker.parameter('RT_RESONANCE_COUNT', 0)) > 0 &&
                Number.isFinite(Number(tracker.parameter('RT_RESONANCE_FREQUENCY_HZ', NaN)))) {
                const currentFrequency = Number(tracker.parameter('RT_RESONANCE_FREQUENCY_HZ', NaN));
                const alreadySaved = baselines.some(function (saved) {
                    return saved.status === 'Complete' && saved.sequence === sequence &&
                        saved.fres === currentFrequency && saved.curve &&
                        saved.curve.frequency[0] === curve.frequency[0] &&
                        saved.curve.frequency.length === curve.frequency.length;
                });
                if (alreadySaved && pendingIteration === null) {
                    lastCapturedSequence = sequence;
                    return;
                }
                let row = baselines.find(function (candidate) {
                    return candidate.iteration === pendingIteration && sequence > candidate.afterSequence;
                });
                if (!row) {
                    row = {iteration: nextIteration++, checked: true};
                    baselines.push(row);
                }
                row.status = 'Complete';
                row.sequence = sequence;
                row.curve = curve;
                row.fres = currentFrequency;
                row.q = Number(tracker.parameter('RT_RESONANCE_Q', NaN));
                row.fwhm = Number(tracker.parameter('RT_RESONANCE_FWHM_HZ', NaN));
                row.fit = Boolean(tracker.parameter('RT_RESONANCE_MODEL_VALID', false)) ? 'Complex' : 'Fallback';
                row.noise = row.fit === 'Complex' && Boolean(tracker.parameter('RT_RESONANCE_NOISE_VALID', false)) ?
                    Number(tracker.parameter('RT_RESONANCE_NOISE', NaN)) : NaN;
                row.quality = row.fit === 'Complex' ? Number(tracker.parameter('RT_RESONANCE_MODEL_QUALITY', NaN)) : NaN;
                row.slope = row.fit === 'Complex' && Boolean(tracker.parameter('RT_RESONANCE_LOCAL_SLOPE_VALID', false)) ?
                    Number(tracker.parameter('RT_RESONANCE_LOCAL_SLOPE_PER_HZ', NaN)) : NaN;
                row.se = Number(tracker.parameter('RT_RESONANCE_SE_HZ', NaN));
                if (baselines.length > maxBaselines) baselines.shift();
                pendingIteration = null;
                lastCapturedSequence = sequence;
                saveBaselines();
                if (tracker.store.activeView === 'tuning') update();
            }
        }
        if (pendingIteration !== null) {
            const state = Number(tracker.parameter('RT_STATE', 0));
            const row = baselines.find(function (candidate) { return candidate.iteration === pendingIteration; });
            if (row && (state === 10 || (complete && !valid && sequence > row.afterSequence))) {
                row.status = state === 10 ? 'Failed' : 'Rejected';
                pendingIteration = null;
                saveBaselines();
            } else if (row && (state === 2 || state === 4)) {
                row.seenActive = true;
            } else if (row && row.seenActive && !complete && state !== 2 && state !== 4) {
                row.status = 'Cancelled';
                pendingIteration = null;
                saveBaselines();
            }
        }
    }

    function drawBaseline(selected, estimates, targetFrequency, targetSeparation) {
        const canvas = byId('tuning-plot');
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, canvas.width, canvas.height);
        if (!selected.length) return null;
        const finiteMagnitude = [];
        selected.forEach(function (row) {
            row.curve.magnitude.filter(Number.isFinite).forEach(function (value) { finiteMagnitude.push(value); });
            if (row.curve.minimum && Number.isFinite(row.curve.minimum.magnitude))
                finiteMagnitude.push(row.curve.minimum.magnitude);
        });
        if (!finiteMagnitude.length) return null;
        const xMinimum = Math.min.apply(null, selected.map(function (row) { return row.curve.frequency[0]; }));
        const xMaximum = Math.max.apply(null, selected.map(function (row) {
            return row.curve.frequency[row.curve.frequency.length - 1];
        }));
        const xSpan = xMaximum - xMinimum || 1;
        const yMinimum = Math.min.apply(null, finiteMagnitude);
        const yMaximum = Math.max.apply(null, finiteMagnitude);
        const ySpan = yMaximum - yMinimum || 1;
        const left = 54, right = canvas.width - 18, top = 20, bottom = canvas.height - 34;
        const x = function (value) { return left + (value - xMinimum) / xSpan * (right - left); };
        const y = function (value) { return bottom - (value - yMinimum) / ySpan * (bottom - top); };

        context.font = '11px monospace';
        context.fillStyle = '#8d9d98';
        context.fillText((xMinimum / 1e6).toFixed(3) + ' MHz', left, canvas.height - 10);
        context.textAlign = 'right';
        context.fillText((xMaximum / 1e6).toFixed(3) + ' MHz', right, canvas.height - 10);
        context.fillText(yMaximum.toFixed(4), left - 7, top + 4);
        context.fillText(yMinimum.toFixed(4), left - 7, bottom);
        context.textAlign = 'left';

        selected.forEach(function (row) {
            const data = row.curve;
            const color = colors[(row.iteration - 1) % colors.length];
            context.strokeStyle = color;
            context.lineWidth = row === selected[selected.length - 1] ? 2 : 1.5;
            context.beginPath();
            data.frequency.forEach(function (frequency, index) {
                if (index) context.lineTo(x(frequency), y(data.magnitude[index]));
                else context.moveTo(x(frequency), y(data.magnitude[index]));
            });
            context.stroke();
            context.fillStyle = color;
            context.fillText('#' + row.iteration, x(data.frequency[0]) + 4,
                y(data.magnitude[0]) - 5);
        });

        const latest = selected[selected.length - 1].curve;
        if (latest.minimum) {
            context.fillStyle = '#69d7c6';
            context.beginPath();
            context.arc(x(latest.minimum.frequency), y(latest.minimum.magnitude), 5, 0, 2 * Math.PI);
            context.fill();
        }

        function marker(frequency, color, dashed) {
            if (!Number.isFinite(frequency) || frequency < xMinimum || frequency > xMaximum) return;
            context.strokeStyle = color;
            context.lineWidth = 2;
            context.setLineDash(dashed ? [6, 5] : []);
            context.beginPath(); context.moveTo(x(frequency), top); context.lineTo(x(frequency), bottom); context.stroke();
            context.setLineDash([]);
        }
        Object.keys(estimates.frequencies).forEach(function (sensor) {
            marker(estimates.frequencies[sensor], '#f58a75', false);
        });
        marker(targetFrequency, '#9d8cff', true);
        if (targetSeparation > 0) marker(targetFrequency + targetSeparation, '#9d8cff', true);
        return latest.minimum;
    }

    function update() {
        bindControls();
        const targetFrequency = Number(byId('tuning-target-frequency').value);
        const targetSeparation = Math.abs(Number(byId('tuning-target-separation').value));
        const tolerance = Math.abs(Number(byId('tuning-tolerance').value));
        const estimates = resonanceEstimates();
        const frequency1 = Number(estimates.frequencies[1]);
        const frequency2 = Number(estimates.frequencies[2]);
        setNumber('tuning-frequency-1', frequency1, 1);
        setNumber('tuning-frequency-2', frequency2, 1);
        renderError('tuning-error-1', frequency1, targetFrequency, tolerance);
        renderError('tuning-error-2', frequency2,
            targetSeparation > 0 ? targetFrequency + targetSeparation : NaN, tolerance);
        byId('tuning-source').textContent = estimates.source;
        byId('tuning-source-detail').textContent = estimates.detail;

        const separation = Number.isFinite(frequency1) && Number.isFinite(frequency2) ?
            Math.abs(frequency2 - frequency1) : NaN;
        setNumber('tuning-separation', separation, 1);
        renderError('tuning-separation-error', separation,
            targetSeparation > 0 ? targetSeparation : NaN, tolerance, 'SEPARATION');

        const state = Number(tracker.parameter('RT_STATE', 0));
        const rawAvailable = state === 1 && Boolean(tracker.parameter('RT_VALID', false));
        const incidentMagnitude = rawAvailable ? finiteParameter('RT_INC_MAG') : NaN;
        const referenceMagnitude = rawAvailable ? finiteParameter('RT_REF_MAG') : NaN;
        const trackedReflection = trackedReflectionMagnitude();
        const reflectionMagnitude = Number.isFinite(trackedReflection) ? trackedReflection :
            (rawAvailable && Boolean(tracker.parameter('RT_R_VALID', false)) ? finiteParameter('RT_R_MAG') : NaN);
        setNumber('tuning-incident-amplitude', incidentMagnitude, 1);
        setNumber('tuning-response-amplitude', referenceMagnitude, 1);
        setNumber('tuning-reflection', reflectionMagnitude, 6);
        setNumber('tuning-balance-db', reflectionMagnitude > 0 ? 20 * Math.log10(reflectionMagnitude) : NaN, 3, ' dB');

        const statisticsAvailable = rawAvailable && Number(tracker.parameter('RT_STATS_COUNT', 0)) > 1;
        const ratioStatisticsAvailable = rawAvailable && Number(tracker.parameter('RT_RATIO_STATS_COUNT', 0)) > 1;
        const incidentNoise = statisticsAvailable ? relativeNoise(
            finiteParameter('RT_INC_I_MEAN'), finiteParameter('RT_INC_Q_MEAN'),
            finiteParameter('RT_INC_I_STDDEV'), finiteParameter('RT_INC_Q_STDDEV')) : NaN;
        const referenceNoise = statisticsAvailable ? relativeNoise(
            finiteParameter('RT_REF_I_MEAN'), finiteParameter('RT_REF_Q_MEAN'),
            finiteParameter('RT_REF_I_STDDEV'), finiteParameter('RT_REF_Q_STDDEV')) : NaN;
        const ratioMean = finiteParameter('RT_R_MAG_MEAN');
        const reflectionNoise = ratioStatisticsAvailable && ratioMean > 0 ?
            100 * finiteParameter('RT_R_MAG_STDDEV') / ratioMean : NaN;
        setNumber('tuning-incident-noise', incidentNoise, 4, '%');
        setNumber('tuning-reference-noise', referenceNoise, 4, '%');
        setNumber('tuning-reflection-noise', reflectionNoise, 4, '%');

        const selected = baselines.filter(function (row) { return row.checked && row.status === 'Complete'; });
        const minimum = drawBaseline(selected, estimates, targetFrequency, targetSeparation);
        renderTable();
        const active = state === 2 || state === 4;
        byId('tuning-acquire-button').disabled = pendingIteration !== null || active || state === 5 ||
            (state >= 6 && state <= 9);
        byId('tuning-cancel-button').disabled = !active;
        setNumber('tuning-min-reflection', minimum ? minimum.magnitude : NaN, 6);
        byId('tuning-min-reflection-frequency').textContent = minimum ?
            'at ' + minimum.frequency.toFixed(1) + ' Hz / ' + minimum.scope : 'no complete refinement';
        byId('tuning-summary').textContent = minimum ?
            selected.length + ' checked baseline(s). Latest checked minimum |R| in ' + minimum.scope +
                ' at ' + minimum.frequency.toFixed(1) + ' Hz.' :
            'Check a completed baseline in the table to display its curve.';
    }

    tracker.registerDashboard('tuning', {
        enter: function () { bindControls(); update(); },
        update: update,
        observe: function () { if (controlsBound) captureBaseline(); },
        leave: function () {}
    });
}(window.ResonanceTracker));
