var url = 'ws://' + location.hostname + ':81/';
var log_enabled = false;

// Send data as binary, NOT as text. If Arduino or ESP will log some special (non printable) character,
// it will ruin text-based web-socket, but binary-based web-sockets handle it well.
var connection = new WebSocket(url, ['arduino']);
connection.binaryType = "arraybuffer";

connection.onopen = function () {
};

connection.onerror = function (error) {
  console.log("WebSocket error ", error);
};

connection.onmessage = function (message) {
  var log_view = document.getElementById("debug_log");
  //log_view.innerHTML += message.data;  // For text-based web-socket
  log_view.innerText += new TextDecoder().decode(message.data);  // For binary-based web-socket
  log_view.scrollTop = log_view.scrollHeight;  // Auto-scroll
};

connection.onclose = function () {
  console.log("WebSocket connection closed");
};

function toggle_reading_logs() {
  console.log("Toggle reading logs");
  if (log_enabled) {
    log_enabled = false;
    document.getElementById("reading_logs_button").textContent = "Start reading logs";
    connection.send("stop_reading_logs");
  } else {
    log_enabled = true;
    document.getElementById("reading_logs_button").textContent = "Stop reading logs";
    connection.send("start_reading_logs");
  }
}

function send_arduino_command() {
  var message = document.getElementById("arduino_command").value;
  if (message.length == 0) {
    return;
  }

  connection.send("arduino_command " + message);
  document.getElementById("arduino_command").value = "";
}
