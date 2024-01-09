
function drpai_model_start_toggle(ws, ev)
{
	var model = document.getElementById("drpai_model_sel");
	var play = (ev.currentTarget.value == "Start");

	const msg_json = {
		"name" : play ? "drpai-model-start" : "drpai-model-stop",
		"value" : { "model": model.value },
	};

	model.disabled = play;

	ws.send(JSON.stringify(msg_json));

	// FIXME: bind this to server response
	ev.currentTarget.value = play ? "Stop" : "Start";
}

function drpai_model_selection_change(ev)
{
	var start = document.getElementById("drpai_model_start");
	start.disabled = (ev.currentTarget.value == "");
}

function drpai_models_get_request(ws) {
	const msg_json = { "name" : "drpai-models-get" };
	ws.send(JSON.stringify(msg_json));
}

function drpai_models_populate_model_names(ws, msg)
{
	var sel = document.getElementById("drpai_model_sel");
	var start = document.getElementById("drpai_model_start");

	start.disabled = true;

	if (!Array.isArray(msg.models) || msg.models.length == 0) {
		sel.innerHTML = '<option value="" hidden>No models available...</option>';
		return;
	}

	var models = [ "<option value='' selected>Select model...</option>" ];
	for (let model of msg.models) {
		models.push(`<option value="${model}">${model}</option>`);
	}

	sel.innerHTML = models.join();
	sel.addEventListener('change', drpai_model_selection_change);

	// Register event listener when the play button gets pushed
	start.addEventListener('click', function(ev) {
		drpai_model_start_toggle(ws, ev);
	});
}

function drpai_models_get_response(ws, msg)
{
	drpai_models_populate_model_names(ws, msg);
}

function connect_drpai_socket()
{
	const callbacks = {
		"drpai-models-get": drpai_models_get_response,
	};

	function drpai_models_get_request(ws) {
		const msg_json = { "name" : "drpai-models-get" };
		ws.send(JSON.stringify(msg_json));
	}

	function handle_json_response(msg) {
		var msg = JSON.parse(msg.data);
		if (!Object.hasOwn(msg, 'name'))
			return;
		if (!Object.hasOwn(callbacks, msg.name))
			return;
		let cb = callbacks[msg.name];
		cb(ws, Object.hasOwn(msg, "value") ? msg.value : null);
	}

	let ws = new_ws("drpai");
        ws.binaryType = "arraybuffer";
	try {
		ws.onopen = function() {
			drpai_models_get_request(ws);
		};

		ws.onmessage = function got_packet(msg) {
			handle_json_response(msg);
		};

		ws.onclose = function(){
		};

	} catch (exception) {

	}
}

connect_drpai_socket();

