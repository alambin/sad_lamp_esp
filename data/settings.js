var url = 'ws://' + location.hostname + ':81/';
var log_enabled = false;

// Send data as binary, NOT as text. If Arduino or ESP will log some special (non printable) character,
// it will ruin text-based web-socket, but binary-based web-sockets handle it well.
var connection = new WebSocket(url, ['arduino']);
connection.binaryType = "arraybuffer";

connection.onerror = function (error) {
  console.log("WebSocket error: ", error);
};

connection.onopen = function () {
  console.log("WebSocket connection opened");
};

connection.onclose = function () {
  console.log("WebSocket connection closed");
};

connection.onmessage = function (message) {
  // Dispatch incomming responses based on current command
  var response = new TextDecoder().decode(message.data);
  switch (command_in_progress) {
    case upload_arduino_firmware_cmd:
      handle_upload_arduino_firmware_response(response);
      break;
    case reboot_arduino_cmd:
      handle_reboot_arduino_cmd(response);
      break;
    default:
      console.log("ERROR: unknownd response: " + response);
      break;
  }
};

var uploaded_file_size = 0;

var command_in_progress = "";
var upload_arduino_firmware_cmd = "upload_arduino_firmware";
var reboot_arduino_cmd = "reboot_arduino";

function _(element) {
  return document.getElementById(element);
}

function upload_esp_file() {
  var file = _("uploaded_file_name").files[0];
  uploaded_file_size = file.size;
  // alert(file.name+" | "+file.size+" | "+file.type);
  var formdata = new FormData();
  formdata.append("uploaded_file", file);

  var ajax = new XMLHttpRequest();
  ajax.upload.addEventListener("progress", upload_progress_handler, false);
  ajax.addEventListener("load", upload_complete_handler, false);
  // ajax.onload = function (e) {
  //   console.log("LAMBIN onload");
  //   if (this.status == 200) {
  //     console.log(this.response);
  //     // this.responseText
  //   }
  // };
  ajax.addEventListener("error", upload_error_handler, false);
  ajax.addEventListener("abort", upload_abort_handler, false);
  ajax.open("POST", "/update");
  ajax.send(formdata);
}

function upload_progress_handler(event) {
  // event.total contains size of uploaded file + some other data. Adjust it to display only file size.
  var extra_data_size = event.total - uploaded_file_size;
  var loaded = event.loaded - extra_data_size;
  if (loaded < 0) loaded = 0;

  _("uploaded_size").innerHTML = "Uploaded " + loaded + " bytes of " + uploaded_file_size;
  var percent = (loaded / uploaded_file_size) * 100;
  _("upload_progress_bar").value = Math.round(percent);
  _("upload_status").innerHTML = Math.round(percent) + "% uploaded... please wait";
}

function upload_complete_handler(event) {
  _("upload_status").innerHTML = event.target.responseText;
  _("upload_progress_bar").value = 0; //will clear progress bar after successful upload
  window.location.href = "/";
}

function upload_error_handler(event) {
  _("upload_status").innerHTML = "Upload Failed!";
}

function upload_abort_handler(event) {
  _("upload_status").innerHTML = "Upload Aborted!";
}

function upload_arduino_file() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  _("upload_arduino_firmware_status").innerHTML = "";
  _("upload_arduino_firmware_status").style.color = "black";
  command_in_progress = upload_arduino_firmware_cmd;
  connection.send(command_in_progress + " \"" + _("arduino_bin_path").value + "\"");
}

function set_server_response(element, response) {
  element.innerHTML = "Server response: " + response;
  if (response.startsWith("ERROR")) {
    command_in_progress = "";
    element.style.color = "red";
    return;
  }
  if (response == "DONE") {
    command_in_progress = "";
    element.style.color = "green";
    return;
  }
}

function handle_upload_arduino_firmware_response(response) {
  var view = _("upload_arduino_firmware_status");
  set_server_response(view, response);
}

function reset_wifi_settings() {
  if (confirm("Are you sure you want to reset WiFi settings?")) {
    var post_request = new XMLHttpRequest();
    post_request.open("POST", "/reset_wifi_settings", false);
    post_request.send();
    _("esp_reboot_status").innerHTML = "Server response: connect to WiFi access point \"SAD-Lamp_AP\" to configure WiFi settings";
    alert("WiFi settings cleared, ESP is rebooting. Connect to WiFi access point \"SAD-Lamp_AP\" to configure WiFi settings");
  }
}

function reboot_esp() {
  if (confirm("Are you sure you want to reboot ESP?")) {
    var post_request = new XMLHttpRequest();
    post_request.open("POST", "/reboot_esp", false);
    post_request.send();
    alert("ESP is rebooting...");
    _("esp_reboot_status").innerHTML = "Server response: ESP reboot in progress...";
    window.location.href = "/";
  }
}

function reboot_arduino() {
  // if (confirm("Are you sure you want to reboot Arduino?")) {
  //   var post_request = new XMLHttpRequest();
  //   post_request.open("POST", "/reboot_arduino", false);
  //   post_request.send();
  //   _("arduino_reboot_status").innerHTML = "Server response: Arduino reboot in progress...";
  // }

  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  if (confirm("Are you sure you want to reboot Arduino?")) {
    _("arduino_reboot_status").innerHTML = "";
    _("arduino_reboot_status").style.color = "black";
    command_in_progress = reboot_arduino_cmd;
    connection.send(command_in_progress);
  }
}

function handle_reboot_arduino_cmd(response) {
  var view = _("arduino_reboot_status");
  set_server_response(view, response);
}
