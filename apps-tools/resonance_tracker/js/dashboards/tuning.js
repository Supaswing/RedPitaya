(function (tracker) {
    const byId = function (id) { return document.getElementById(id); };
    let controlsBound = false;

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
        loadSetting('tuning-target-frequency', 30000000);
        loadSetting('tuning-target-separation', 0);
        loadSetting('tuning-tolerance', 1000);
        ['tuning-target-frequency','tuning-target-separation','tuning-tolerance'].forEach(function (id) {
            byId(id).addEventListener('change', function () {
                saveSetting(id);
                update();
            });
        });
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
        return {
            frequency: frequency,
            magnitude: real.map(function (value, index) { return Math.hypot(value, imag[index]); })
        };
    }

    function drawBaseline(data, estimates, targetFrequency, targetSeparation) {
        const canvas = byId('tuning-plot');
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, canvas.width, canvas.height);
        if (!data) return null;
        const finiteMagnitude = data.magnitude.filter(Number.isFinite);
        if (!finiteMagnitude.length) return null;
        const xMinimum = data.frequency[0];
        const xMaximum = data.frequency[data.frequency.length - 1];
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

        context.strokeStyle = '#d5f36a';
        context.lineWidth = 2;
        context.beginPath();
        data.frequency.forEach(function (frequency, index) {
            if (index) context.lineTo(x(frequency), y(data.magnitude[index]));
            else context.moveTo(x(frequency), y(data.magnitude[index]));
        });
        context.stroke();

        let minimumIndex = 0;
        data.magnitude.forEach(function (value, index) {
            if (value < data.magnitude[minimumIndex]) minimumIndex = index;
        });
        context.fillStyle = '#69d7c6';
        context.beginPath();
        context.arc(x(data.frequency[minimumIndex]), y(data.magnitude[minimumIndex]), 5, 0, 2 * Math.PI);
        context.fill();

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
        return {frequency: data.frequency[minimumIndex], magnitude: data.magnitude[minimumIndex]};
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

        const data = baselineData();
        const minimum = drawBaseline(data, estimates, targetFrequency, targetSeparation);
        setNumber('tuning-min-reflection', minimum ? minimum.magnitude : NaN, 6);
        byId('tuning-min-reflection-frequency').textContent = minimum ?
            'at ' + minimum.frequency.toFixed(1) + ' Hz' : 'no complete scan';
        byId('tuning-summary').textContent = minimum ?
            'Minimum measured |R| at ' + minimum.frequency.toFixed(1) + ' Hz. Targets and fitted resonances are overlaid.' :
            'No completed coherent baseline is available.';
    }

    tracker.registerDashboard('tuning', {
        enter: function () { bindControls(); update(); },
        update: update,
        leave: function () {}
    });
}(window.ResonanceTracker));
