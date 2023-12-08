

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

function new_ws(protocol)
{
	return new WebSocket(get_appropriate_ws_url(""), protocol);
}

function load_js_script(url) {
	var script = document.createElement("script");
	script.src = "js/" + url;
	document.body.appendChild(script);
}

const js_scripts = [
	"camera.js",
];

js_scripts.forEach(load_js_script);

