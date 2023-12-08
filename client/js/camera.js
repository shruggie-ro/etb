
function camera_devices_get_request(ws)
{
	const msg_json = { "name" : "camera-devices-get" };
	ws.send(JSON.stringify(msg_json));
}

function camera_devices_get_response(ws, msg)
{
	var cam_sel = document.getElementById("camera_device_sel");

	if (!Array.isArray(msg) || msg.length == 0) {
		cam_sel.innerHTML = '<option value="" hidden>No camera device...</option>';
		return;
	}

	var devices = [ "<option value='' selected>Select camera device...</option>" ];
	for (let dev of msg) {
		devices.push(`<option value="${dev.device}">${dev.card}</option>`);
	}

	cam_sel.innerHTML = devices.join();
}

const camera_callbacks = {
	"onopen": camera_devices_get_request,
	"onmessage": camera_devices_get_response,
};

command_socket_register_callbacks("camera-devices-get", camera_callbacks);
