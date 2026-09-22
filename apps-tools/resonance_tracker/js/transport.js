(function (global) {
    const tracker = global.ResonanceTracker;
    const appId = 'resonance_tracker';
    const startUrl = '/bazaar?start=' + appId + '?type=run';
    const stopUrl = '/bazaar?stop=' + appId;
    const socketProtocol = global.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const socketUrl = socketProtocol + global.location.host + '/wss';
    let socket = null;
    let stopping = false;
    let reconnectTimer = null;

    function showConnection(message, isError) {
        const badge = document.getElementById('connection');
        badge.textContent = message;
        badge.classList.toggle('error', Boolean(isError));
    }

    function send(parameters) {
        if (!socket || socket.readyState !== WebSocket.OPEN) {
            showConnection('DISCONNECTED', true);
            return false;
        }
        socket.send(JSON.stringify({parameters: parameters}));
        return true;
    }

    function retryStart() {
        if (stopping || reconnectTimer !== null) return;
        reconnectTimer = global.setTimeout(function () {
            reconnectTimer = null;
            start();
        }, 2000);
    }

    function connect() {
        const parser = new BinarySignalParser();
        socket = new WebSocket(socketUrl);
        socket.binaryType = 'arraybuffer';
        socket.onopen = function () {
            showConnection('CONNECTED', false);
            send({in_command: {value: 'send_all_params'}});
        };
        socket.onclose = function () {
            socket = null;
            showConnection('DISCONNECTED', true);
            retryStart();
        };
        socket.onerror = function () { showConnection('CONNECTION ERROR', true); };
        socket.onmessage = function (event) {
            try {
                const parsed = parser.convert(event.data);
                tracker.publish(parsed.parameters, parsed.signals);
            } catch (error) {
                console.error('Unable to parse Red Pitaya data', error);
            }
        };
    }

    function start() {
        showConnection('STARTING', false);
        global.jQuery.ajax({url: startUrl, type: 'GET', timeout: 10000}).done(function (response) {
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

    tracker.transport = {start: start, send: send};
    global.addEventListener('beforeunload', function () {
        stopping = true;
        if (reconnectTimer !== null) global.clearTimeout(reconnectTimer);
        if (socket) {
            socket.onclose = null;
            socket.close();
        }
        global.jQuery.ajax({url: stopUrl, type: 'GET', async: false});
    });
}(window));
