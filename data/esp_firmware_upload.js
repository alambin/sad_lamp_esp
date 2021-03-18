var uploaded_file_size = 0;

function _(element) {
  return document.getElementById(element);
}

function upload_file() {
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
}

function upload_error_handler(event) {
  _("upload_status").innerHTML = "Upload Failed!";
}

function upload_abort_handler(event) {
  _("upload_status").innerHTML = "Upload Aborted!";
}