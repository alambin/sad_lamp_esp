var url = 'ws://' + location.hostname + ':81/';
var alarm_enabled = false;

// Send data as binary, NOT as text. If Arduino or ESP will log some special (non printable) character,
// it will ruin text-based web-socket, but binary-based web-sockets handle it well.
var connection = new WebSocket(url, ['arduino']);
connection.binaryType = "arraybuffer";

connection.onerror = function (error) {
  console.log("WebSocket error: " + error);
};

connection.onopen = function () {
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
      handle_reboot_arduino_response(response);
      break;
    case get_arduino_settings_cmd:
      handle_get_arduino_settings_response(response);
      break;
    case set_arduino_datetime_cmd:
      handle_set_arduino_datetime_response(response);
      break;
    case enable_arduino_alarm_cmd:
      handle_enable_arduino_alarm_response(response);
      break;
    case set_arduino_alarm_time_cmd:
      handle_set_arduino_alarm_time_response(response);
      break;
    case set_arduino_sunrise_duration_cmd:
      handle_set_arduino_sunrise_duration_response(response);
      break;
    case set_arduino_brightness_cmd:
      handle_set_arduino_brightness_response(response);
      break;
    default:
      console.log("ERROR: unknownd response: " + response);
      break;
  }
};

var uploaded_file_size = 0;

var command_in_progress = "";
var upload_esp_firmware_cmd = "upload_esp_firmware";
var upload_arduino_firmware_cmd = "upload_arduino_firmware";
var reboot_arduino_cmd = "reboot_arduino";
var get_arduino_settings_cmd = "get_arduino_settings";
var set_arduino_datetime_cmd = "set_arduino_datetime";
var enable_arduino_alarm_cmd = "enable_arduino_alarm";
var set_arduino_alarm_time_cmd = "set_arduino_alarm_time";
var set_arduino_sunrise_duration_cmd = "set_arduino_sunrise_duration";
var set_arduino_brightness_cmd = "set_arduino_brightness";

function _(element) {
  return document.getElementById(element);
}

function upload_esp_file() {
  if (_("uploaded_file_name").value.length == 0) {
    // User pressed Cancel
    return;
  }

  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    _("uploaded_file_name").value = "";
    return;
  }
  command_in_progress = upload_esp_firmware_cmd;

  var file = _("uploaded_file_name").files[0];
  uploaded_file_size = file.size;
  // alert(file.name+" | "+file.size+" | "+file.type);
  var formdata = new FormData();
  formdata.append("uploaded_file", file);

  var ajax = new XMLHttpRequest();
  ajax.upload.addEventListener("progress", upload_progress_handler, false);
  ajax.addEventListener("load", upload_complete_handler, false);
  ajax.addEventListener("error", upload_error_handler, false);
  ajax.addEventListener("abort", upload_abort_handler, false);
  ajax.open("POST", "/update?file_size=" + uploaded_file_size);
  ajax.send(formdata);

  _("upload_status").style.color = "black";
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
  command_in_progress = "";
  _("upload_status").innerHTML = event.target.responseText;
  if (event.target.responseText.startsWith("ERROR")) {
    _("upload_status").style.color = "red";
  } else {
    _("upload_status").style.color = "green";
    setTimeout(function () {
      window.location.href = "/";
    }, 5000);
  }

  _("upload_progress_bar").value = 0; //will clear progress bar after successful upload
  _("uploaded_file_name").value = "";
}

function upload_error_handler(event) {
  command_in_progress = "";
  _("upload_status").innerHTML = "Upload Failed!";
  _("upload_status").style.color = "red";
  _("upload_progress_bar").value = 0;
  _("uploaded_file_name").value = "";
}

function upload_abort_handler(event) {
  command_in_progress = "";
  _("upload_status").innerHTML = "Upload Aborted!";
  _("upload_status").style.color = "red";
  _("upload_progress_bar").value = 0;
  _("uploaded_file_name").value = "";
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
  command_in_progress = "";
  if (response.startsWith("ERROR")) {
    element.style.color = "red";
    return;
  }
  if (response == "DONE") {
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

function handle_reboot_arduino_response(response) {
  // Do NOT call set_server_response() here, because there are several responses from ESP during reboot and we need to
  // clear command_in_progress only in case of error or successfull finish
  var element = _("arduino_reboot_status");
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

function check_sunrise_duration() {
  var value = _("sunrise_duration").value;
  if (value > 1440) {
    _("sunrise_duration").value = 1440;
  } else if (value < 0) {
    _("sunrise_duration").value = 0;
  }
}

function disable_arduino_time_controls(disable) {
  _("set_datetime").disabled = disable;
  _("datetime").disabled = disable;
}

function disable_arduino_alarm_controls(disable) {
  _("alarm").disabled = disable;
  _("alarm_d0").disabled = disable;
  _("alarm_d1").disabled = disable;
  _("alarm_d2").disabled = disable;
  _("alarm_d3").disabled = disable;
  _("alarm_d4").disabled = disable;
  _("alarm_d5").disabled = disable;
  _("alarm_d6").disabled = disable;
  _("set_alarm").disabled = disable;
}

function disable_arduino_sunrise_duration_controls(disable) {
  _("sunrise_duration").disabled = disable;
  _("set_sunrise_duration").disabled = disable;
}

function disable_arduino_brightness_controls(disable) {
  _("brightness").disabled = disable;
  _("set_brightness").disabled = disable;
}

function disable_arduino_settings_controls(disable) {
  disable_arduino_time_controls(disable);
  disable_arduino_alarm_controls(disable);
  _("enable_alarm").disabled = disable;
  disable_arduino_sunrise_duration_controls(disable);
  disable_arduino_brightness_controls(disable);
}

function refresh_arduino_settings() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  toggle_loading_animation();
  get_arduino_settings();
}

function get_arduino_settings() {
  disable_arduino_settings_controls(true);
  command_in_progress = get_arduino_settings_cmd;
  _("arduino_settings_status").innerHTML = "Updating Arduino settings...";
  _("arduino_settings_status").style.color = "black";
  connection.send(command_in_progress);
}

function handle_get_arduino_settings_response(response) {
  command_in_progress = "";

  var results_json = JSON.parse(response);
  var time_str = results_json["time"];
  var alarm_str = results_json["alarm"];
  var sd_str = results_json["sunrise duration"];
  var brightness_str = results_json["brightness"];

  // Check for errors
  var server_response = "";
  if (time_str.startsWith("ERROR")) {
    server_response += "Get time error: " + time_str + "; ";
  } else {
    set_arduino_time(time_str);
    disable_arduino_time_controls(false);
  }

  if (alarm_str.startsWith("ERROR")) {
    server_response += "Get alarm error: " + alarm_str + "; ";
  } else {
    set_arduino_alarm(alarm_str);
  }

  if (sd_str.startsWith("ERROR")) {
    server_response += "Get sunset duration error: " + sd_str + "; ";
  } else {
    set_arduino_sunrise_duration(sd_str);
    disable_arduino_sunrise_duration_controls(false);
  }

  if (brightness_str.startsWith("ERROR")) {
    server_response += "Get brightness error: " + brightness_str;
  } else {
    set_arduino_brightness(brightness_str);
  }

  if (server_response.length != 0) {
    _("arduino_settings_status").innerHTML = "Server response: " + server_response;
    _("arduino_settings_status").style.color = "red";
  } else {
    _("arduino_settings_status").innerHTML = "Server response: DONE";
    _("arduino_settings_status").style.color = "green";
  }
  toggle_loading_animation();
}

function set_arduino_time(time_str) {
  var year = time_str.substring(15, 19);
  var day = time_str.substring(9, 11);
  var month = time_str.substring(12, 14);
  var hour = time_str.substring(0, 2);
  var minute = time_str.substring(3, 5);
  var sec = time_str.substring(6, 8);
  var input_date = year + "-" + month + "-" + day + "T" + hour + ":" + minute + ":" + sec;
  _("datetime").value = input_date;
}

function update_alarm_controls() {
  disable_arduino_alarm_controls(!alarm_enabled);
  _("enable_alarm").textContent = (alarm_enabled) ? "Disable" : "Enable";
}

function set_arduino_alarm(alarm_str) {
  alarm_enabled = (alarm_str.substring(0, 1) == "E");
  update_alarm_controls();
  _("enable_alarm").disabled = false;

  var hour = alarm_str.substring(2, 4);
  var minute = alarm_str.substring(5, 7);
  _("alarm").value = hour + ":" + minute;

  var dow = alarm_str.substring(8, 10);
  set_dow(dow);
}

function set_dow(dow) {
  var dow_num = Number.parseInt(dow, 16);
  for (i = 0; i < 7; i++) {
    var mask = 1 << i;
    var value = dow_num & mask;
    _("alarm_d" + i).checked = (value != 0);
  }
}

function get_dow() {
  var dow_num = 0;
  for (i = 0; i < 7; i++) {
    if (!_("alarm_d" + i).checked) {
      continue;
    }
    var mask = 1 << i;
    dow_num |= mask;
  }

  return dow_num;
}

function set_arduino_sunrise_duration(sd_str) {
  var sd_num = Number.parseInt(sd_str);
  _("sunrise_duration").value = sd_num;
}

function set_arduino_brightness(brightness_str) {
  var is_auto_mode = (brightness_str.substring(0, 1) == "A");
  disable_arduino_brightness_controls(!is_auto_mode);
  _("brightness_manual_mode_sign").style.visibility = is_auto_mode ? 'hidden' : 'visible';

  var brightness = Number.parseInt(brightness_str.substring(2));
  _("brightness").value = brightness;
}

function check_brightness() {
  var value = _("brightness").value;
  if (value > 1023) {
    _("brightness").value = 1023;
  } else if (value < 0) {
    _("brightness").value = 0;
  }
}

function set_datetime() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  var datetime_str = _("datetime").value;
  var year = datetime_str.substring(0, 4);
  var month = datetime_str.substring(5, 7);
  var day = datetime_str.substring(8, 10);
  var hour = datetime_str.substring(11, 13);
  var min = datetime_str.substring(14, 16);
  var sec = datetime_str.substring(17, 19);
  if (sec.length == 0) {
    sec = "00";
  }
  var arduino_datetime_str = hour + ":" + min + ":" + sec + " " + day + "/" + month + "/" + year;

  _("arduino_settings_status").innerHTML = "Status: setting datetime...";
  _("arduino_settings_status").style.color = "black";
  command_in_progress = set_arduino_datetime_cmd;
  connection.send(command_in_progress + " " + arduino_datetime_str);
}

function handle_set_arduino_datetime_response(response) {
  var view = _("arduino_settings_status");
  set_server_response(view, response);
}

function set_alarm() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  var alarm_str = _("alarm").value;
  var hour = alarm_str.substring(0, 2);
  var min = alarm_str.substring(3, 5);
  var dow = get_dow();
  var arduino_alarm_str = hour + ":" + min + " " + dow.toString(16);

  _("arduino_settings_status").innerHTML = "Status: setting alarm time...";
  _("arduino_settings_status").style.color = "black";
  command_in_progress = set_arduino_alarm_time_cmd;
  connection.send(command_in_progress + " " + arduino_alarm_str);
}

function handle_set_arduino_alarm_time_response(response) {
  var view = _("arduino_settings_status");
  set_server_response(view, response);
}

function enable_alarm() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  _("arduino_settings_status").innerHTML = "Status: " + (alarm_enabled ? "disabling" : "enabling") + " alarm...";
  _("arduino_settings_status").style.color = "black";
  command_in_progress = enable_arduino_alarm_cmd;
  connection.send(command_in_progress + (alarm_enabled ? " D" : " E"));
}

function handle_enable_arduino_alarm_response(response) {
  var view = _("arduino_settings_status");
  set_server_response(view, response);

  alarm_enabled = !alarm_enabled;
  update_alarm_controls();
}

function set_sunrise_duration() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  _("arduino_settings_status").innerHTML = "Status: setting sunrise duration...";
  _("arduino_settings_status").style.color = "black";
  command_in_progress = set_arduino_sunrise_duration_cmd;
  connection.send(command_in_progress + " " + _("sunrise_duration").value.toString().padStart(4, "0"));
}

function handle_set_arduino_sunrise_duration_response(response) {
  var view = _("arduino_settings_status");
  set_server_response(view, response);
}

function set_brightness() {
  if (command_in_progress.length != 0) {
    alert("ERROR: command \"" + command_in_progress + "\" is still in progress");
    return;
  }

  _("arduino_settings_status").innerHTML = "Status: setting brightness...";
  _("arduino_settings_status").style.color = "black";
  command_in_progress = set_arduino_brightness_cmd;
  connection.send(command_in_progress + " " + _("brightness").value.toString().padStart(4, "0"));
}

function handle_set_arduino_brightness_response(response) {
  var view = _("arduino_settings_status");
  set_server_response(view, response);
}

function toggle_loading_animation() {
  _("table_wrapper").classList.toggle("loading");
  _("inactive_table_background").classList.toggle("inactive_table_background");
}
