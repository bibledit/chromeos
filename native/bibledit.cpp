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


#include "bibledit.h"
#include <sys/stat.h>
#include <sys/mount.h>
#include <thread>
#include <iostream>
#include <fstream>
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/instance.h"
#include "nacl_io/nacl_io.h"


pp::Instance * pepper_instance = nullptr;
pp::Module * pepper_module = nullptr;
thread * main_worker_thread = nullptr;


void initialize_nacl_io ()
{
  // Initialize nacl_io (NaCl IO) with PPAPI support.
  nacl_io_init_ppapi (pepper_instance->pp_instance (), pepper_module->Get ()->get_browser_interface ());
  
  // Messages sent to /dev/console0 appear in the app's Chrome development console.
  int result = mount ("", "console0", "dev", 0, "");
  if (result != 0) {
    cerr << strerror (errno) << endl;
  }

  // By default, nacl_io mounts "/" to pass through to the original NaCl filesystem.
  // That doesn't do much.
  // Remount it to a memfs filesystem.
  result = umount ("/");
  if (result != 0) {
    post_message_to_console (strerror (errno));
  }

  // The memory file system is lightning fast.
  // Use it for fast volatile storage.
  result = mount ("", "/", "memfs", 0, "");
  if (result != 0) {
    post_message_to_console (strerror (errno));
  }

  // The persistent file system is slow.
  // Mount it at "/webroot".
  mkdir ("/persistent", 0777);
  result = mount ("", "/webroot", "html5fs", 0, "type=PERSISTENT,expected_size=10737418240");
  if (result != 0) {
    post_message_to_console (strerror (errno));
  }
  
  // The files in the app package are accessible via http (using the URLLoader API).
  // Alternatively you can use nacl_io to expose the http resources via the POSIX filesystem API.
  // For example:  mount("/", "/mnt/http", "httpfs", 0, "");
  // The files in your package can then be access via open/fopen of "/mnt/http/<filename>".
  result = mount ("", "/http", "httpfs", 0, "");
  if (result != 0) {
    post_message_to_console (strerror (errno));
  }
}


void destroy_nacl_io ()
{
  // Remove interception for POSIX C-library function and release associated resources.
  nacl_io_uninit ();
}


void main_worker_thread_function ()
{
  initialize_nacl_io ();
  
  post_message_to_gui ("openbrowser");
  post_message_to_gui ("Bibledit ready");
  
  // BIBLEDIT_LIBRARY_CALLS
  
  destroy_nacl_io ();
}


// The Instance class.
// One of these exists for each instance of your NaCl module on the web page.
// The browser will ask the Module object to create a new Instance
// for each occurrence of the <embed> tag that has these attributes:
// * src="bibledit.nmf"
// * type="application/x-pnacl"
// To communicate with the browser, you must override HandleMessage () to receive messages from the browser,
// and use PostMessage() to send messages back to the browser.
// Note that this interface is asynchronous.
// The constructor creates the plugin-side instance.
// @param[in] instance the handle to the browser-side plugin instance.
class BibleditInstance : public pp::Instance
{
public:
  explicit BibleditInstance (PP_Instance instance);
  virtual ~BibleditInstance ();
  virtual void HandleMessage (const pp::Var& var_message);
};


BibleditInstance::BibleditInstance (PP_Instance instance) : pp::Instance (instance)
{
  // Global pointer to the Pepper instance.
  pepper_instance = this;
  // Main thread for the work to be done.
  main_worker_thread = new thread (main_worker_thread_function);
}


BibleditInstance::~BibleditInstance ()
{
  // Wait till the thread completes.
  main_worker_thread->join ();
}


// Handler for messages coming in from the browser via postMessage().
// The @a var_message can contain be any pp:Var type; for example int, string Array or Dictionary.
// Please see the pp:Var documentation for more details.
// @param[in] var_message The message posted by the browser.
void BibleditInstance::HandleMessage (const pp::Var& var_message)
{
  // Make this function handle the incoming message.
  // Ignore the message if it is not a string.
  if (!var_message.is_string()) return;
  // Get the string message and compare it to "hello".
  string message = var_message.AsString ();
  if (message == "hello") {
    // If it matches, send a response back to JavaScript.
    pp::Var var_reply ("Hello");
    PostMessage (var_reply);
  }
}


// The Module class.
// The browser calls the CreateInstance() method to create an instance of your NaCl module on the web page.
// The browser creates a new instance for each <embed> tag with type="application/x-pnacl".
class BibleditModule : public pp::Module
{
public:
  BibleditModule ();
  virtual ~BibleditModule ();
  virtual pp::Instance* CreateInstance (PP_Instance instance);
};


BibleditModule::BibleditModule ()
{
  // Global pointer to the Pepper module.
  pepper_module = this;
}


BibleditModule::~BibleditModule ()
{
}


// Create and return a BibleditInstance object.
// @param[in] instance The browser-side instance.
// @return the plugin-side instance.
pp::Instance* BibleditModule::CreateInstance (PP_Instance instance)
{
  return new BibleditInstance (instance);
}


namespace pp {
  // Factory function called by the browser when the module is first loaded.
  // The browser keeps a singleton of this module.
  // It calls the CreateInstance() method on the object you return to create an instance.
  // There is one instance per <embed> tag on the page.
  // This is the main binding point for your NaCl module with the browser.
  Module * CreateModule ()
  {
    return new BibleditModule ();
  }
}


// Post a message to the app's window.
void post_message_to_gui (const string & msg)
{
  pp::Var var_reply (msg);
  pepper_instance->PostMessage (var_reply);
}


// Post a message to the app's development console.
void post_message_to_console (const string & msg)
{
  try {
    ofstream file;
    file.open ("/dev/console0");
    file << msg;
    file.close ();
  } catch (...) {
  }
}
