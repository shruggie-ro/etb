

function load_js_script(url) {
	var script = document.createElement("script");
	script.src = "js/" + url;
	document.body.appendChild(script);
}

const js_scripts = [
	"camera.js",
	"websocket.js", // needs to be last
];

let command_socket = null;
let command_callback_table = new Map();

function command_socket_register_callbacks(name, callbacks)
{
	command_callback_table.set(name, callbacks);
}

js_scripts.forEach(load_js_script);

