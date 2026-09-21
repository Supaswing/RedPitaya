(function () {
    const app = {
        id: 'resonance_tracker',
        ws: null,
        params: {},
        signals: {},
        running: false,
        stopping: false,
        reconnectTimer: null
    };
    const startUrl = '/bazaar?start=' + app.id + '?type=run';
    const stopUrl = '/bazaar?stop=' + app.id;
    const socketProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const socketUrl = socketProtocol + window.location.host + '/wss';
    const byId = (id) => document.getElementById(id);
    function value(name, fallback) { return app.params[name] && app.params[name].value !== undefined ? app.params[name].value : fallback; }
    function renderNumber(id, parameter, digits, available) {
        const number = Number(value(parameter, NaN));
        byId(id).textContent = available && Number.isFinite(number) ? number.toFixed(digits) : '-';
    }
    function send(parameters) {
        if (!app.ws || app.ws.readyState !== WebSocket.OPEN) {
            showConnection('DISCONNECTED', true);
            return false;
        }
        app.ws.send(JSON.stringify({ parameters }));
        return true;
    }
    function draw() {
        const canvas = byId('plot'), ctx = canvas.getContext('2d'), inc = app.signals.RT_HISTORY_INC_MAG, ref = app.signals.RT_HISTORY_REF_MAG;
        ctx.clearRect(0, 0, canvas.width, canvas.height); if (!inc || !inc.value || inc.value.length < 2) return;
        const refValues = ref && Array.isArray(ref.value) ? ref.value : [];
        const all = inc.value.concat(refValues).filter(Number.isFinite), max = Math.max.apply(null, all) || 1;
        [['#d5f36a', inc.value], ['#69d7c6', ref && ref.value]].forEach(function (line) { if (!line[1]) return; ctx.strokeStyle=line[0]; ctx.lineWidth=2; ctx.beginPath(); line[1].forEach(function (point, index) { const x=index/(line[1].length-1)*canvas.width, y=canvas.height-12-point/max*(canvas.height-24); index ? ctx.lineTo(x,y) : ctx.moveTo(x,y); }); ctx.stroke(); });
    }
    function render() {
        const state = ['STOPPED','RUNNING','ERROR'][value('RT_STATE',0)] || 'UNKNOWN';
        app.running = Boolean(value('RT_RUN', false));
        byId('state').textContent=state; byId('error').textContent=value('RT_ERROR','No backend error') || 'No backend error'; byId('sequence').textContent=value('RT_SEQUENCE',0); byId('validity').textContent=value('RT_VALID',false)?'VALID':'INVALID'; byId('busy').textContent=value('RT_BUSY',false)?'BUSY':'IDLE'; byId('overflow').textContent=value('RT_OVERFLOW',false)?'OVERFLOW':'OVERFLOW: UNAVAILABLE'; byId('requested').textContent=value('RT_REQUESTED_FREQUENCY_HZ','-'); byId('effective').textContent=value('RT_EFFECTIVE_FREQUENCY_HZ','-'); byId('rate').textContent=Number(value('RT_ACQUISITION_RATE_HZ',0)).toFixed(1); byId('publication-rate').textContent=Number(value('RT_PUBLICATION_RATE_HZ',0)).toFixed(1); byId('connection').textContent=app.ws && app.ws.readyState===1?'CONNECTED':'DISCONNECTED';
        byId('run').textContent=app.running?'STOP':'RUN'; byId('run').className=app.running?'stop':'run';
        if (document.activeElement !== byId('frequency')) byId('frequency').value=value('RT_FREQUENCY_HZ',32000000);
        if (document.activeElement !== byId('window-shift')) byId('window-shift').value=value('RT_WINDOW_SHIFT',17);
        if (document.activeElement !== byId('interval')) byId('interval').value=value('RT_TELEMETRY_MS',50);
        renderNumber('integration-samples', 'RT_INTEGRATION_SAMPLES', 0, true);
        renderNumber('integration-time', 'RT_INTEGRATION_TIME_US', 3, true);
        byId('effective-window-shift').textContent=value('RT_EFFECTIVE_WINDOW_SHIFT',17);
        [['inc-i','RT_INC_I'],['inc-q','RT_INC_Q'],['inc-mag','RT_INC_MAG'],['inc-phase','RT_INC_PHASE_DEG'],['ref-i','RT_REF_I'],['ref-q','RT_REF_Q'],['ref-mag','RT_REF_MAG'],['ref-phase','RT_REF_PHASE_DEG']].forEach(function (x) { const v=value(x[1],'-'); byId(x[0]).textContent=typeof v==='number'?v.toFixed(x[1].includes('PHASE')||x[1].includes('MAG')?3:0):v; });
        const sampleCount = Number(value('RT_STATS_COUNT', 0));
        const ratioCount = Number(value('RT_RATIO_STATS_COUNT', 0));
        const ratioValid = Boolean(value('RT_R_VALID', false));
        byId('stats-count').textContent = sampleCount;
        byId('ratio-stats-count').textContent = ratioCount;
        byId('ratio-validity').textContent = ratioValid ? 'VALID' : 'INVALID';
        [['ratio-real','RT_R_REAL'],['ratio-imag','RT_R_IMAG'],['ratio-mag','RT_R_MAG'],['ratio-phase','RT_R_PHASE_DEG']].forEach(function (x) { renderNumber(x[0], x[1], 6, ratioValid); });
        [['inc-i-mean','RT_INC_I_MEAN',2],['inc-i-stddev','RT_INC_I_STDDEV',2],['inc-q-mean','RT_INC_Q_MEAN',2],['inc-q-stddev','RT_INC_Q_STDDEV',2],['ref-i-mean','RT_REF_I_MEAN',2],['ref-i-stddev','RT_REF_I_STDDEV',2],['ref-q-mean','RT_REF_Q_MEAN',2],['ref-q-stddev','RT_REF_Q_STDDEV',2]].forEach(function (x) { renderNumber(x[0], x[1], x[2], sampleCount > 0); });
        [['ratio-real-mean','RT_R_REAL_MEAN'],['ratio-real-stddev','RT_R_REAL_STDDEV'],['ratio-imag-mean','RT_R_IMAG_MEAN'],['ratio-imag-stddev','RT_R_IMAG_STDDEV'],['ratio-mag-mean','RT_R_MAG_MEAN'],['ratio-mag-stddev','RT_R_MAG_STDDEV'],['ratio-mean-phase','RT_R_MEAN_PHASE_DEG']].forEach(function (x) { renderNumber(x[0], x[1], 6, ratioCount > 0); });
        draw();
    }
    function showConnection(message, isError) {
        byId('connection').textContent = message;
        byId('connection').classList.toggle('error', Boolean(isError));
    }
    function retryStart() {
        if (app.stopping || app.reconnectTimer !== null) return;
        app.reconnectTimer = window.setTimeout(function () {
            app.reconnectTimer = null;
            startApp();
        }, 2000);
    }
    function connect() {
        const parser = new BinarySignalParser();
        app.ws = new WebSocket(socketUrl);
        app.ws.binaryType = 'arraybuffer';
        app.ws.onopen = function () {
            showConnection('CONNECTED', false);
            send({in_command:{value:'send_all_params'}});
        };
        app.ws.onclose = function () {
            app.ws = null;
            showConnection('DISCONNECTED', true);
            retryStart();
        };
        app.ws.onerror = function () { showConnection('CONNECTION ERROR', true); };
        app.ws.onmessage = function (event) {
            try {
                const parsed = parser.convert(event.data);
                Object.assign(app.params, parsed.parameters || {});
                Object.assign(app.signals, parsed.signals || {});
                render();
            } catch (error) {
                console.error('Unable to parse Red Pitaya data', error);
            }
        };
    }
    function startApp() {
        showConnection('STARTING', false);
        window.jQuery.ajax({url:startUrl, type:'GET', timeout:10000}).done(function (response) {
            if (response && response.status === 'OK') connect();
            else {
                showConnection('START FAILED', true);
                retryStart();
            }
        }).fail(function () {
            showConnection('START FAILED', true);
            retryStart();
        });
    }
    byId('run').onclick=function(){
        const requestedRun = !app.running;
        if (send({RT_RUN:{value:requestedRun}})) {
            byId('run').textContent=requestedRun?'STARTING':'STOPPING';
            byId('run').disabled=true;
            window.setTimeout(function(){ byId('run').disabled=false; }, 500);
        }
    };
    byId('frequency').onchange=function(){ send({RT_FREQUENCY_HZ:{value:Number(byId('frequency').value)}}); };
    byId('window-shift').onchange=function(){ send({RT_WINDOW_SHIFT:{value:Number(byId('window-shift').value)}}); };
    byId('interval').onchange=function(){ send({RT_TELEMETRY_MS:{value:Number(byId('interval').value)}}); };
    window.addEventListener('beforeunload', function () {
        app.stopping = true;
        if (app.reconnectTimer !== null) window.clearTimeout(app.reconnectTimer);
        if (app.ws) {
            app.ws.onclose = null;
            app.ws.close();
        }
        window.jQuery.ajax({url:stopUrl, type:'GET', async:false});
    });
    window.setTimeout(startApp, 250);
}());
