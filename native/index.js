/*
Copyright (©) 2003-2016 Teus Benschop.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
  
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


$ (document).ready (function () {
  // Handle the native code events.
  var listener = document.getElementById ("listener");
  listener.addEventListener ("loadstart", moduleDidStartLoad, true);
  listener.addEventListener ("progress", moduleLoadProgress, true);
  listener.addEventListener ("error", moduleLoadError, true);
  listener.addEventListener ("abort", moduleLoadAbort, true);
  listener.addEventListener ("load", moduleDidLoad, true);
  listener.addEventListener ("loadend", moduleDidEndLoad, true);
  listener.addEventListener ("crash", moduleDidCrash, true);
  listener.addEventListener ("message", handleMessage, true);
});


// Global application object.
var BibleditModule = null;


// Handler that gets called when the NaCl module starts loading.  This
// event is always triggered when an <EMBED> tag has a MIME type of
// application/x-nacl.
function moduleDidStartLoad() {
  updateStatus ("Start module load");
}


// Progress event handler.
// |event| contains a couple of interesting properties that are used in this example:
// * total The size of the NaCl module in bytes.
//   Note that this value is 0 until |lengthComputable| is true.
//   In particular, this value is 0 for the first "progress" event.
// * loaded The number of bytes loaded so far.
// * lengthComputable A boolean indicating that the |total| field represents a valid length.
// event The ProgressEvent that triggered this handler.
function moduleLoadProgress (event) {
  var loadPercent = 0.0;
  var loadPercentString;
  if (event.lengthComputable && event.total > 0) {
    loadPercent = event.loaded / event.total * 100.0;
    loadPercentString = loadPercent + "%";
    updateStatus ("Loading " + loadPercentString + " (" + event.loaded + " of " + event.total + " bytes)");
  } else {
    // The total length is not yet known.
    updateStatus ("Loading...");
  }
}


// Handler that gets called if an error occurred while loading the NaCl
// module.  Note that the event does not carry any meaningful data about
// the error, you have to check lastError on the <EMBED> element to find
// out what happened.
function moduleLoadError() {
  updateStatus ("Error " + common.naclModule.lastError);
}


// Handler that gets called if the NaCl module load is aborted.
function moduleLoadAbort() {
  updateStatus ("Abort");
}


// Indicate load success.
function moduleDidLoad() {
  updateStatus ("Module loaded");
  // Send a message to the Native Client module
  BibleditModule = document.getElementById ("bibledit");
  BibleditModule.postMessage ("hello");
}


// Handler that gets called when the NaCl module loading has completed.
// You will always get one of these events, regardless of whether the NaCl
// module loaded successfully or not.  For example, if there is an error
// during load, you will get an "error" event and a "loadend" event.  Note
// that if the NaCl module loads successfully, you will get both a "load"
// event and a "loadend" event.
function moduleDidEndLoad () {
  var lastError = event.target.lastError;
  if (lastError == undefined || lastError.length == 0) {
    lastError = "<none>";
  }
  updateStatus("Finished loading, last error: " + lastError);
}


function moduleDidCrash () {
  var lastError = event.target.exitStatus;
  updateStatus("Crashed with exit status " + lastError);
}


// The "message" event handler.
// This handler is fired when the NaCl module posts a message to the browser
// by calling PPB_Messaging.PostMessage() (in C) or pp::Instance.PostMessage() (in C++).
function handleMessage (message_event) {
  updateStatus (message_event.data);
}


// If the page loads before the Native Client module loads, then set the
// status message indicating that the module is still loading.  Otherwise,
// do not change the status message.
function pageDidLoad () {
  if (BibleditModule == null) {
    updateStatus ("Loading...");
  } else {
    // It"s possible that the Native Client module onload event fired
    // before the page"s onload event.  In this case, the status message
    // will reflect "SUCCESS", but won"t be displayed.  This call will
    // display the current message.
    updateStatus ("Page loaded");
  }
}


// Appends the $opt_message to the status output in the app.
function updateStatus (opt_message) {
  var statusField = document.getElementById ("statusField");
  //opt_message = "<br>" + opt_message;
  //statusField.innerHTML += opt_message;
  statusField.innerHTML = opt_message;
}

