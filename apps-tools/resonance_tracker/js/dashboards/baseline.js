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
        const filteredMagnitude = values('RT_BASELINE_FILTERED_MAG');
        const curvature = values('RT_BASELINE_CURVATURE');
        const candidateLeft = values('RT_CANDIDATE_LEFT_HZ');
        const candidateRight = values('RT_CANDIDATE_RIGHT_HZ');
        const candidateScore = values('RT_CANDIDATE_SCORE');
        const candidateIsInflection = values('RT_CANDIDATE_IS_INFLECTION');
        const refineSensor = values('RT_REFINE_SENSOR_ID');
        const refineFrequency = values('RT_REFINE_FREQUENCY_HZ');
        const refineReal = values('RT_REFINE_RE');
        const refineImag = values('RT_REFINE_IM');
        const modelSensor = values('RT_MODEL_SENSOR_ID');
        const modelFrequency = values('RT_MODEL_FREQUENCY_HZ');
        const modelReal = values('RT_MODEL_RE');
        const modelImag = values('RT_MODEL_IM');
        const fitFrequency = values('RT_FIT_FREQUENCY_HZ');
        const fitFwhm = values('RT_FIT_FWHM_HZ');
        const filterRadius = Number(tracker.parameter('RT_BASELINE_FILTER_RADIUS', 5));
        const canvas = byId('baseline-plot');
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, canvas.width, canvas.height);
        if (signalSequence.length !== 1 || Number(signalSequence[0]) !== parameterSequence ||
            frequency.length < 2 || real.length !== frequency.length || imag.length !== frequency.length) return false;
        const magnitude = real.map(function (value, index) { return Math.hypot(value, imag[index]); });
        const refineMagnitude = refineReal.map(function (value, index) { return Math.hypot(value, refineImag[index]); });
        const modelMagnitude = modelReal.map(function (value, index) { return Math.hypot(value, modelImag[index]); });
        const filteredForScale = filteredMagnitude.length === frequency.length ?
            filteredMagnitude.slice(filterRadius, filteredMagnitude.length - filterRadius) : [];
        const allMagnitude = magnitude.concat(refineMagnitude, modelMagnitude, filteredForScale).filter(Number.isFinite);
        const minimum = Math.min.apply(null, allMagnitude);
        const maximum = Math.max.apply(null, allMagnitude);
        const span = maximum - minimum || 1;
        const xMinimum = Number(frequency[0]);
        const xMaximum = Number(frequency[frequency.length - 1]);
        const xSpan = xMaximum - xMinimum || 1;
        const left = 42, right = canvas.width - 14, top = 18, bottom = canvas.height - 28;
        const curvatureTop = bottom - 86;
        const magnitudeBottom = curvatureTop - 18;
        const x = function (value) { return left + (Number(value) - xMinimum) / xSpan * (right - left); };
        const y = function (value) {
            return magnitudeBottom - (Number(value) - minimum) / span * (magnitudeBottom - top);
        };

        function filteredAt(targetFrequency) {
            if (filteredMagnitude.length !== frequency.length) return NaN;
            const position = (Number(targetFrequency) - xMinimum) / xSpan * (filteredMagnitude.length - 1);
            if (position < 0 || position > filteredMagnitude.length - 1) return NaN;
            const lower = Math.floor(position);
            const upper = Math.min(lower + 1, filteredMagnitude.length - 1);
            const fraction = position - lower;
            return Number(filteredMagnitude[lower]) + fraction *
                (Number(filteredMagnitude[upper]) - Number(filteredMagnitude[lower]));
        }

        context.font = '11px monospace';
        context.fillStyle = '#8d9d98';
        context.fillText((xMinimum / 1e6).toFixed(3) + ' MHz', left, canvas.height - 8);
        context.textAlign = 'right';
        context.fillText((xMaximum / 1e6).toFixed(3) + ' MHz', right, canvas.height - 8);
        context.textAlign = 'left';

        candidateLeft.forEach(function (value, index) {
            if (index >= candidateRight.length) return;
            const x0 = x(value), x1 = x(candidateRight[index]);
            context.fillStyle = index ? 'rgba(105,215,198,.10)' : 'rgba(213,243,106,.10)';
            context.fillRect(x0, top, Math.max(1, x1 - x0), magnitudeBottom - top);
            context.strokeStyle = index ? '#69d7c6' : '#d5f36a';
            context.setLineDash([4, 4]);
            context.strokeRect(x0, top, Math.max(1, x1 - x0), magnitudeBottom - top);
            context.setLineDash([]);
            context.fillStyle = context.strokeStyle;
            context.fillText('C' + (index + 1) + ' ' + Number(candidateScore[index] || 0).toExponential(2), x0 + 4, top + 13);
        });

        fitFrequency.forEach(function (center, index) {
            if (index >= fitFwhm.length) return;
            const halfWidth = 0.5 * Number(fitFwhm[index]);
            const x0 = x(Number(center) - halfWidth), x1 = x(Number(center) + halfWidth);
            context.fillStyle = 'rgba(245,138,117,.08)';
            context.fillRect(x0, top, Math.max(1, x1 - x0), magnitudeBottom - top);
            context.strokeStyle = '#f58a75';
            context.lineWidth = 1;
            context.beginPath(); context.moveTo(x(center), top); context.lineTo(x(center), magnitudeBottom); context.stroke();
        });

        function line(frequencies, magnitudes, color, dashed, points) {
            if (frequencies.length < 2 || frequencies.length !== magnitudes.length) return;
            context.strokeStyle = color;
            context.fillStyle = color;
            context.lineWidth = 2;
            context.setLineDash(dashed ? [7, 5] : []);
            context.beginPath();
            frequencies.forEach(function (value, index) {
                if (index) context.lineTo(x(value), y(magnitudes[index]));
                else context.moveTo(x(value), y(magnitudes[index]));
            });
            context.stroke();
            context.setLineDash([]);
            if (points) frequencies.forEach(function (value, index) {
                context.fillRect(x(value) - 2, y(magnitudes[index]) - 2, 4, 4);
            });
        }

        line(frequency, magnitude, '#d5f36a', false, false);
        if (filteredMagnitude.length === frequency.length && frequency.length > 2 * filterRadius) {
            line(frequency.slice(filterRadius, frequency.length - filterRadius),
                filteredMagnitude.slice(filterRadius, filteredMagnitude.length - filterRadius),
                '#ffffff', true, false);
        }
        candidateLeft.forEach(function (leftFrequency, index) {
            if (index >= candidateRight.length || Number(candidateIsInflection[index]) !== 1) return;
            [leftFrequency, candidateRight[index]].forEach(function (pointFrequency) {
                const pointMagnitude = filteredAt(pointFrequency);
                if (!Number.isFinite(pointMagnitude)) return;
                const px = x(pointFrequency), py = y(pointMagnitude);
                context.fillStyle = index ? '#69d7c6' : '#d5f36a';
                context.strokeStyle = '#101817';
                context.lineWidth = 1;
                context.beginPath();
                context.arc(px, py, 5, 0, 2 * Math.PI);
                context.fill();
                context.stroke();
            });
        });

        const curvatureEdge = filterRadius + 1;
        if (curvature.length === frequency.length && frequency.length > 2 * curvatureEdge) {
            const curvatureValues = curvature.slice(curvatureEdge, curvature.length - curvatureEdge).map(Number);
            const curvatureFrequencies = frequency.slice(curvatureEdge, frequency.length - curvatureEdge);
            const maximumAbsoluteCurvature = Math.max.apply(null,
                curvatureValues.map(function (value) { return Math.abs(value); }).filter(Number.isFinite)) || 1;
            const curvatureMiddle = 0.5 * (curvatureTop + bottom);
            const curvatureHalfHeight = 0.5 * (bottom - curvatureTop) - 5;
            const curvatureY = function (value) {
                return curvatureMiddle - Number(value) / maximumAbsoluteCurvature * curvatureHalfHeight;
            };
            context.fillStyle = '#8d9d98';
            context.fillText('signed curvature', left, curvatureTop - 4);
            context.textAlign = 'right';
            context.fillText('+/- ' + maximumAbsoluteCurvature.toExponential(2), right, curvatureTop - 4);
            context.textAlign = 'left';
            context.strokeStyle = '#53615e';
            context.lineWidth = 1;
            context.beginPath(); context.moveTo(left, curvatureMiddle); context.lineTo(right, curvatureMiddle); context.stroke();
            context.strokeStyle = '#ffcf66';
            context.lineWidth = 2;
            context.beginPath();
            curvatureFrequencies.forEach(function (value, index) {
                const py = curvatureY(curvatureValues[index]);
                if (index) context.lineTo(x(value), py);
                else context.moveTo(x(value), py);
            });
            context.stroke();
            candidateLeft.forEach(function (leftFrequency, index) {
                if (index >= candidateRight.length || Number(candidateIsInflection[index]) !== 1) return;
                [leftFrequency, candidateRight[index]].forEach(function (pointFrequency) {
                    const px = x(pointFrequency);
                    context.strokeStyle = index ? '#69d7c6' : '#d5f36a';
                    context.setLineDash([3, 3]);
                    context.beginPath(); context.moveTo(px, curvatureTop); context.lineTo(px, bottom); context.stroke();
                    context.setLineDash([]);
                    context.fillStyle = context.strokeStyle;
                    context.beginPath(); context.arc(px, curvatureMiddle, 4, 0, 2 * Math.PI); context.fill();
                });
            });
        }
        ['#69d7c6', '#9d8cff'].forEach(function (color, sensorIndex) {
            const sensorId = sensorIndex + 1;
            const refineF = [], refineM = [], modelF = [], modelM = [];
            refineSensor.forEach(function (id, index) {
                if (Number(id) === sensorId && index < refineFrequency.length && index < refineMagnitude.length) {
                    refineF.push(refineFrequency[index]); refineM.push(refineMagnitude[index]);
                }
            });
            modelSensor.forEach(function (id, index) {
                if (Number(id) === sensorId && index < modelFrequency.length && index < modelMagnitude.length) {
                    modelF.push(modelFrequency[index]); modelM.push(modelMagnitude[index]);
                }
            });
            line(refineF, refineM, color, false, true);
            line(modelF, modelM, '#f0a35e', true, false);
        });
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
                RT_BASELINE_SENSOR_COUNT: {value: Number(byId('baseline-sensors').value)},
                RT_BASELINE_OVERVIEW_POINTS: {value: Number(byId('baseline-overview-points').value)},
                RT_BASELINE_FILTER_RADIUS: {value: Number(byId('baseline-filter-radius').value)},
                RT_BASELINE_COARSE_AVERAGES: {value: Number(byId('baseline-coarse-averages').value)},
                RT_BASELINE_REFINE_POINTS: {value: Number(byId('baseline-refine-points').value)},
                RT_BASELINE_REFINE_AVERAGES: {value: Number(byId('baseline-refine-averages').value)}
            });
        });
        byId('baseline-cancel-button').addEventListener('click', function () { tracker.sendCommand(2); });
        byId('baseline-start').addEventListener('change', function () { sendConfiguration('baseline-start', 'RT_BASELINE_START_HZ'); });
        byId('baseline-stop').addEventListener('change', function () { sendConfiguration('baseline-stop', 'RT_BASELINE_STOP_HZ'); });
        byId('baseline-sensors').addEventListener('change', function () { sendConfiguration('baseline-sensors', 'RT_BASELINE_SENSOR_COUNT'); });
        byId('baseline-overview-points').addEventListener('change', function () { sendConfiguration('baseline-overview-points', 'RT_BASELINE_OVERVIEW_POINTS'); });
        byId('baseline-filter-radius').addEventListener('change', function () { sendConfiguration('baseline-filter-radius', 'RT_BASELINE_FILTER_RADIUS'); });
        byId('baseline-coarse-averages').addEventListener('change', function () { sendConfiguration('baseline-coarse-averages', 'RT_BASELINE_COARSE_AVERAGES'); });
        byId('baseline-refine-points').addEventListener('change', function () { sendConfiguration('baseline-refine-points', 'RT_BASELINE_REFINE_POINTS'); });
        byId('baseline-refine-averages').addEventListener('change', function () { sendConfiguration('baseline-refine-averages', 'RT_BASELINE_REFINE_AVERAGES'); });
    }

    function update() {
        const valid = Boolean(tracker.parameter('RT_BASELINE_VALID', false));
        const complete = Boolean(tracker.parameter('RT_BASELINE_COMPLETE', false));
        const progress = Number(tracker.parameter('RT_BASELINE_PROGRESS', 0));
        const state = Number(tracker.parameter('RT_STATE', 0));
        const active = state === 2 || state === 4;
        if (active && lastSequence !== -1) {
            const context = byId('baseline-plot').getContext('2d');
            context.clearRect(0, 0, context.canvas.width, context.canvas.height);
            lastSequence = -1;
        }
        byId('baseline-summary').textContent = valid ? 'Last completed baseline is valid.' :
            (active ? 'Baseline acquisition and resonance finding are active.' :
                (complete ? 'The completed baseline was rejected; see the backend error.' : 'No completed baseline is available.'));
        byId('baseline-progress').textContent = progress.toFixed(1);
        byId('baseline-start-button').disabled = active || state === 5;
        byId('baseline-cancel-button').disabled = !active;
        byId('baseline-start').disabled = active;
        byId('baseline-stop').disabled = active;
        byId('baseline-sensors').disabled = active;
        byId('baseline-overview-points').disabled = active;
        byId('baseline-filter-radius').disabled = active;
        byId('baseline-coarse-averages').disabled = active;
        byId('baseline-refine-points').disabled = active;
        byId('baseline-refine-averages').disabled = active;
        synchronizeInput('baseline-start', 'RT_BASELINE_START_HZ', 30000000);
        synchronizeInput('baseline-stop', 'RT_BASELINE_STOP_HZ', 34000000);
        synchronizeInput('baseline-sensors', 'RT_BASELINE_SENSOR_COUNT', 1);
        synchronizeInput('baseline-overview-points', 'RT_BASELINE_OVERVIEW_POINTS', 101);
        synchronizeInput('baseline-filter-radius', 'RT_BASELINE_FILTER_RADIUS', 5);
        synchronizeInput('baseline-coarse-averages', 'RT_BASELINE_COARSE_AVERAGES', 3);
        synchronizeInput('baseline-refine-points', 'RT_BASELINE_REFINE_POINTS', 21);
        synchronizeInput('baseline-refine-averages', 'RT_BASELINE_REFINE_AVERAGES', 3);
        const measurementWindows = Number(byId('baseline-overview-points').value) *
            Number(byId('baseline-coarse-averages').value) + Number(byId('baseline-sensors').value) *
            (Number(byId('baseline-refine-points').value) * Number(byId('baseline-refine-averages').value) + 5);
        byId('baseline-measurements').textContent = measurementWindows.toFixed(0);
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
