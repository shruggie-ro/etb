
function get_camera_devices_request(ws)
{
	const msg_json = { "name" : "get-camera-devices" };
	ws.send(JSON.stringify(msg_json));
}

function get_camera_devices_response(ws, msg)
{
	var cam_sel = document.getElementById("camera_device");

	if (!Array.isArray(msg) || msg.length == 0) {
		cam_sel.innerHTML = '<option value="" hidden>No camera device...</option>';
		return;
	}

	var devices = [ "<option value='' selected>Select camera device...</option>" ];
	for (let dev of msg) {
		devices.push(`<option value="${dev}">${dev}</option>`);
	}

	cam_sel.innerHTML = devices.join();
}

const camera_callbacks = {
	"onopen": get_camera_devices_request,
	"onmessage": get_camera_devices_response,
};

command_socket_register_callbacks("get-camera-devices", camera_callbacks);
