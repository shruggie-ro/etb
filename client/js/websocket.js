
function get_appropriate_ws_url(extra_url)
{
	var pcol;
	var u = document.URL;

	/*
	 * We open the websocket encrypted if this page came on an
	 * https:// url itself, otherwise unencrypted
	 */

	if (u.substring(0, 5) === "https") {
		pcol = "wss://";
		u = u.substr(8);
	} else {
		pcol = "ws://";
		if (u.substring(0, 4) === "http")
			u = u.substr(7);
	}

	u = u.split("/");

	/* + "/xxx" bit is for IE10 workaround */

	return pcol + u[0] + "/" + extra_url;
}

function new_ws(urlpath, protocol)
{
	return new WebSocket(urlpath, protocol);
}

function connect_command_socket() {
	command_socket = new_ws(get_appropriate_ws_url(""), "command");
	try {
		command_socket.onopen = function() {
			for (let [name, callbacks] of command_callback_table) {
				if (Object.hasOwn(callbacks, "onopen"))
					callbacks.onopen(command_socket);
			}
		};

		command_socket.onmessage = function got_packet(msg) {
			var msg = JSON.parse(msg.data);
			if (!Object.hasOwn(msg, 'name'))
				return;
			var cb = command_callback_table.get(msg.name);
			if (cb && Object.hasOwn(cb, "onmessage")) {
				var val = Object.hasOwn(msg, "value") ? msg.value : null;
				cb.onmessage(command_socket, val);
			}
		};

		command_socket.onclose = function(){
			for (let [name, callbacks] of command_callback_table) {
				if (Object.hasOwn(callbacks, "onclose"))
					callbacks.onopen(command_socket);
			}
		};

	} catch (exception) {
		command_socket = null;
	}
}

// This needs to be called last
connect_command_socket();
