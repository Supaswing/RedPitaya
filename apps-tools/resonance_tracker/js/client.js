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
    function send(parameters) { if (app.ws && app.ws.readyState === WebSocket.OPEN) app.ws.send(JSON.stringify({ parameters })); }
    function draw() {
        const canvas = byId('plot'), ctx = canvas.getContext('2d'), inc = app.signals.RT_HISTORY_INC_MAG, ref = app.signals.RT_HISTORY_REF_MAG;
        ctx.clearRect(0, 0, canvas.width, canvas.height); if (!inc || !inc.value || inc.value.length < 2) return;
        const all = inc.value.concat(ref ? ref.value : []), max = Math.max.apply(null, all) || 1;
        [['#d5f36a', inc.value], ['#69d7c6', ref && ref.value]].forEach(function (line) { if (!line[1]) return; ctx.strokeStyle=line[0]; ctx.lineWidth=2; ctx.beginPath(); line[1].forEach(function (point, index) { const x=index/(line[1].length-1)*canvas.width, y=canvas.height-12-point/max*(canvas.height-24); index ? ctx.lineTo(x,y) : ctx.moveTo(x,y); }); ctx.stroke(); });
    }
    function render() {
        const state = ['STOPPED','RUNNING','ERROR'][value('RT_STATE',0)]; byId('state').textContent=state; byId('error').textContent=value('RT_ERROR','No backend error') || 'No backend error'; byId('sequence').textContent=value('RT_SEQUENCE',0); byId('validity').textContent=value('RT_VALID',false)?'VALID':'INVALID'; byId('overflow').textContent=value('RT_OVERFLOW',false)?'OVERFLOW':'OVERFLOW: UNAVAILABLE'; byId('effective').textContent=value('RT_EFFECTIVE_FREQUENCY_HZ','-'); byId('rate').textContent=Number(value('RT_ACQUISITION_RATE_HZ',0)).toFixed(1); byId('connection').textContent=app.ws && app.ws.readyState===1?'CONNECTED':'DISCONNECTED';
        [['inc-i','RT_INC_I'],['inc-q','RT_INC_Q'],['inc-mag','RT_INC_MAG'],['inc-phase','RT_INC_PHASE_DEG'],['ref-i','RT_REF_I'],['ref-q','RT_REF_Q'],['ref-mag','RT_REF_MAG'],['ref-phase','RT_REF_PHASE_DEG']].forEach(function (x) { const v=value(x[1],'-'); byId(x[0]).textContent=typeof v==='number'?v.toFixed(x[1].includes('PHASE')||x[1].includes('MAG')?3:0):v; }); draw();
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
    byId('run').onclick=function(){ app.running=!app.running; send({RT_RUN:{value:app.running}}); byId('run').textContent=app.running?'STOP':'RUN'; byId('run').className=app.running?'stop':''; };
    byId('frequency').onchange=function(){ send({RT_FREQUENCY_HZ:{value:Number(byId('frequency').value)}}); };
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
