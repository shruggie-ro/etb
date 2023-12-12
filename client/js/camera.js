
function camera_device_play_toggle(ws, ev)
{
	var cam_sel = document.getElementById("camera_device_sel");
	var play = (ev.currentTarget.value == "Play");

	const msg_json = {
		"name" : play ? "camera-device-play" : "camera-device-stop",
		"value" : { "device": cam_sel.value },
	};

	cam_sel.disabled = play;

	ws.send(JSON.stringify(msg_json));

	// FIXME: bind this to server response
	ev.currentTarget.value = play ? "Stop" : "Play";
}

function camera_device_selection_change(ev)
{
	var cam_play = document.getElementById("camera_device_play");
	var val = ev.currentTarget.value;
	cam_play.disabled = (val == "");
}

function camera_devices_get_request(ws)
{
	const msg_json = { "name" : "camera-devices-get" };
	ws.send(JSON.stringify(msg_json));
}

function camera_devices_get_response(ws, msg)
{
	var cam_sel = document.getElementById("camera_device_sel");
	var cam_play = document.getElementById("camera_device_play");

	if (!Array.isArray(msg) || msg.length == 0) {
		cam_sel.innerHTML = '<option value="" hidden>No camera device...</option>';
		cam_play.disabled = true;
		return;
	}

	var devices = [ "<option value='' selected>Select camera device...</option>" ];
	for (let dev of msg) {
		devices.push(`<option value="${dev.device}">${dev.card}</option>`);
	}

	// Register event listener when camera device changes
	cam_sel.innerHTML = devices.join();
	cam_sel.addEventListener('change', camera_device_selection_change);

	// Register event listener when the play button gets pushed
	cam_play.addEventListener('click', function(ev) {
		camera_device_play_toggle(ws, ev);
	});
}

// adapted from: https://github.com/oatpp/example-yuv-websocket-stream/blob/master/res/cam/wsImageView.html
function yuv2CanvasImageData(canvas, data)
{
	let msg_array = new Uint8ClampedArray(data);

	if (msg_array.length == 0)
		return;

	let context = canvas.getContext("2d");
	let imgData = context.createImageData(640, 480);
	let i, j;

	for (i = 0, j = 0, g = 0; i < imgData.data.length && j < msg_array.length; i += 8, j += 4, g+= 2) {
		const y1 = msg_array[j  ];
		const u  = msg_array[j+1];
		const y2 = msg_array[j+2];
		const v  = msg_array[j+3];

		imgData.data[i    ] = Math.min(255, Math.max(0, Math.floor(y1+1.4075*(v-128))));
		imgData.data[i + 1] = Math.min(255, Math.max(0, Math.floor(y1-0.3455*(u-128)-(0.7169*(v-128)))));
		imgData.data[i + 2] = Math.min(255, Math.max(0, Math.floor(y1+1.7790*(u-128))));
		imgData.data[i + 3] = 255;
		imgData.data[i + 4] = Math.min(255, Math.max(0, Math.floor(y2+1.4075*(v-128))));
		imgData.data[i + 5] = Math.min(255, Math.max(0, Math.floor(y2-0.3455*(u-128)-(0.7169*(v-128)))));
		imgData.data[i + 6] = Math.min(255, Math.max(0, Math.floor(y2+1.7790*(u-128))));
		imgData.data[i + 7] = 255;
	}
	context.putImageData(imgData, 0, 0);
}

function connect_camera_socket()
{
	const camera_callbacks = {
		"camera-devices-get": camera_devices_get_response,
	};

	function handle_binary_response(msg) {
		let canvas = document.getElementById("default_canvas");

		yuv2CanvasImageData(canvas, msg.data);
	}

	function handle_json_response(msg) {
		var msg = JSON.parse(msg.data);
		if (!Object.hasOwn(msg, 'name'))
			return;
		if (!Object.hasOwn(camera_callbacks, msg.name))
			return;
		let cb = camera_callbacks[msg.name];
		cb(ws, Object.hasOwn(msg, "value") ? msg.value : null);
	}

	let ws = new_ws("camera");
        ws.binaryType = "arraybuffer";
	try {
		ws.onopen = function() {
			camera_devices_get_request(ws);
		};

		ws.onmessage = function got_packet(msg) {
			if (msg.data instanceof ArrayBuffer) {
				handle_binary_response(msg);
			} else {
				handle_json_response(msg);
			}
		};

		ws.onclose = function(){
		};

	} catch (exception) {

	}
}

connect_camera_socket();

