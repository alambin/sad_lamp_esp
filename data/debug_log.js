var url = 'ws://' + location.hostname + ':81/';
var connection = new WebSocket(url, ['arduino']);
var log_enabled = false;

connection.onopen = function () {
  connection.send('SAD-Lamp web UI is connected on ' + new Date());
};

connection.onerror = function (error) {
  console.log('WebSocket Error ', error);
};

connection.onmessage = function (message) {
  console.log('Server: ', message.data);
  document.getElementById("debug_log").innerHTML += message.data;
};

connection.onclose = function () {
  console.log('WebSocket connection closed');
};

function toggle_reading_logs() {
  console.log('Toggle reading logs');
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
