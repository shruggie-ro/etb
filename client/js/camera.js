let predictionData = null; // FIXME hack
let predictionImage = null; // FIXME hack

function camera_device_play_toggle_button(ws, buttonElement) {
    var sel = document.getElementById("camera_device_sel");
    var res_sel = document.getElementById("camera_resolution_sel");
    var play = (buttonElement.value == "Play");

    var selectedResolution = res_sel.value;
    var [width, height] = [0, 0];
    if (selectedResolution) {
        [width, height] = selectedResolution.split('x').map(Number);
    }

    const msg_json = {
        "name": play ? "camera-device-play" : "camera-device-stop",
        "value": {
            "device": sel.value,
            "resolution": {
                "width": width,
                "height": height
            }
        }
    };

    console.log("Selected device:", sel.value);
    console.log("Selected resolution:", width, height);

    // Send message to server
    ws.send(JSON.stringify(msg_json));

    // Toggle button value
    if (play) {
        buttonElement.value = "Stop";
        sel.disabled = true;
        res_sel.disabled = true;
    } else {
        buttonElement.value = "Play";
        sel.disabled = false;
        res_sel.disabled = false;
    }
}

function camera_device_play_toggle(ws, ev) {
    camera_device_play_toggle_button(ws, ev.currentTarget);
}

function camera_device_selection_change(ev) {
    var play = document.getElementById("camera_device_play");
    play.disabled = (ev.currentTarget.value == "");
}

function camera_devices_get_request(ws) {
    const msg_json = { "name": "camera-devices-get" };
    ws.send(JSON.stringify(msg_json));
}

function camera_devices_get_response(ws, msg) {
    var sel = document.getElementById("camera_device_sel");
    var res_sel = document.getElementById("camera_resolution_sel");
    var play = document.getElementById("camera_device_play");
    console.log(msg);

    play.disabled = true;

    if (!Array.isArray(msg) || msg.length == 0) {
        sel.innerHTML = '<option value="" hidden>No camera device...</option>';
        res_sel.innerHTML = '<option value="" hidden>No resolution...</option>';
        return;
    }

    var devices = ["<option value='' selected>Select camera device...</option>"];
    for (let dev of msg) {
        devices.push(`<option value="${dev.device}">${dev.card}</option>`);
    }

    var resolutions = ["<option value='' selected>Select resolution...</option>"];
    for (let dev of msg) {
        for (let res of dev.resolutions) {
            resolutions.push(`<option value="${res.width}x${res.height}">${res.width} x ${res.height}</option>`);
        }
    }

    sel.innerHTML = devices.join('');
    res_sel.innerHTML = resolutions.join('');
    sel.addEventListener('change', camera_device_selection_change);

    play.addEventListener('click', function(ev) {
        camera_device_play_toggle_button(ws, ev);
    });

    // Select the first available device
    if (msg.length > 0) {
        sel.selectedIndex = 1; // Set to the first device
    }
    
    // Re-enable the button for play
    play.disabled = false;
    // Set the button state to "Play"
    play.value = "Play";
}

function yuv2CanvasImageData(canvas, data) {
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

function drpai_handle_object_detection_result(ws, msg) {
    if (!Array.isArray(msg) || msg.length == 0) {
        predictionData = null;
        return;
    }

    predictionData = msg; // FIXME hack
}

function connect_camera_socket() {
    let startTime = null;
    let updateElapsedTimeCounter = 0;
    const elapsedTimeFormat = { hour: "numeric", minute: "numeric", second: "numeric" };

    const callbacks = {
        "camera-devices-get": camera_devices_get_response,
        "drpai-object-detection-result": drpai_handle_object_detection_result,
    };

    function update_elapsed_time() {
        updateElapsedTimeCounter++;
        if (updateElapsedTimeCounter < 5)
            return;
        updateElapsedTimeCounter = 0;

        if (startTime == null)
            startTime = new Date();

        let nowTime = new Date();
        let elapsedTotal = Math.floor((nowTime - startTime) / 1000); // seconds
        let seconds = elapsedTotal % 60;
        elapsedTotal = Math.floor(elapsedTotal / 60);                // minutes
        let minutes = elapsedTotal % 60;
        let hours = Math.floor(elapsedTotal / 60);                   // hours
        let elem = document.getElementById("camera_elapsed_time");
        elem.innerHTML = hours.toString().padStart(2, '0') + ":" +
                     minutes.toString().padStart(2, '0') + ":" +
                     seconds.toString().padStart(2, '0');
    }

    function handle_binary_response(msg) {
        let canvas = document.getElementById("camera_canvas");
        yuv2CanvasImageData(canvas, msg.data);
        update_elapsed_time();
    }

    let imgElemCamera = document.createElement("img");
    let imgElemDrpAi = document.createElement("img");
    function handle_binary_response2(msg) {
        let id = String.fromCharCode.apply(null, new Uint8Array(msg.data, 0, 15));
        let canvas = document.getElementById("camera_canvas");
        let contextCamera = canvas.getContext("2d");

        let base64Image = btoa(String.fromCharCode.apply(null, new Uint8Array(msg.data, 16)));
        if (id.startsWith("drpai+camera"))
            predictionImage = base64Image;

        imgElemCamera.width = 640;
        imgElemCamera.height = 480;
        imgElemCamera.src = "data:image/jpeg;base64," + base64Image;

        contextCamera.drawImage(imgElemCamera, 0, 0, 640, 480);
        if (predictionImage) {
            imgElemDrpAi.width = 640;
            imgElemDrpAi.height = 480;
            imgElemDrpAi.src = "data:image/jpeg;base64," + predictionImage;

            canvas = document.getElementById("drpai_canvas");
            let contextDrpAi = canvas.getContext("2d");
            contextDrpAi.drawImage(imgElemDrpAi, 0, 0, 640, 480);

            if (predictionData) {
                let data = predictionData;
                contextDrpAi.lineWidth = 16;
                contextDrpAi.strokeStyle = 'blue';
                contextDrpAi.fillStyle = 'blue';
                contextDrpAi.font = "24pt";
                for (let i = 0; i < data.length; i++) {
                    let label = data[i].label;
                    let box = data[i].box;
                    contextDrpAi.strokeRect(box.x, box.y, box.w, box.h);
                    contextDrpAi.fillText(label, box.x, (box.y + 16));
                }
            }
        }

        update_elapsed_time();
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

    let ws = new_ws("camera");
    ws.binaryType = "arraybuffer";
    try {
        ws.onopen = function() {
            camera_devices_get_request(ws);
        };

        ws.onmessage = function got_packet(msg) {
            if (msg.data instanceof ArrayBuffer) {
                handle_binary_response2(msg);
            } else {
                handle_json_response(msg);
            }
        };

        ws.onclose = function() {
            // Handle socket close if needed
        };

    } catch (exception) {
        console.error("Error connecting to camera socket:", exception);
    }
}

connect_camera_socket();
