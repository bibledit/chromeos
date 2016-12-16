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


#define __STDC_LIMIT_MACROS
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/ppb_file_io.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/directory_entry.h"
#include "ppapi/cpp/file_io.h"
#include "ppapi/cpp/file_ref.h"
#include "ppapi/cpp/file_system.h"
#include "ppapi/cpp/message_loop.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_array.h"
#include "ppapi/utility/completion_callback_factory.h"

#include <stdio.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <algorithm>
#include <set>
#include <chrono>
#include <iomanip>
#include <stdexcept>
#include <thread>
#include <cmath>
#include <mutex>
#include <numeric>
#include <random>
#include <limits>


using namespace std;


#ifndef INT32_MAX
#define INT32_MAX (0x7FFFFFFF)
#endif


pp::Instance * bibledit_instance;
thread * bibledit_worker_thread;
pp::FileSystem * pepper_file_system;


void bibledit_worker_thread_function ()
{
  cout << "Thread start" << endl;

  // Request 10 Gbyte of space in the persistent local filesystem.
  // The Chrome app's manifest requests the "unlimitedStorage" permission already.
  // Google Chrome stores its persistent files in this folder:
  // ~/Library/Application\ Support/Google/Chrome/Default/File\ System
  pepper_file_system = new pp::FileSystem (bibledit_instance, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  int result = pepper_file_system->Open (10 * 1024 * 1024 * 1024, pp::BlockUntilComplete());
  if (result != PP_OK) {
    pepper_file_system = nullptr;
    cout << "Failed to open local persistent file system with error " << result << endl;
  }
  cout << "Thread complete" << endl;
}


/*
 The Instance class.
 One of these exists for each instance of your NaCl module on the web page.
 The browser will ask the Module object to create a new Instance 
 for each occurrence of the <embed> tag that has these attributes:
 * src="bibledit.nmf"
 * type="application/x-pnacl"
 To communicate with the browser, you must override HandleMessage() 
 to receive messages from the browser, 
 and use PostMessage() to send messages back to the browser.
 Note that this interface is asynchronous.
*/
class BibleditInstance : public pp::Instance {
public:
  // The constructor creates the plugin-side instance.
  // @param[in] instance the handle to the browser-side plugin instance.
  explicit BibleditInstance (PP_Instance instance) : pp::Instance (instance)
  {
    bibledit_instance = this;
    bibledit_worker_thread = new thread (bibledit_worker_thread_function);
  }
  
  virtual ~BibleditInstance ()
  {
    bibledit_worker_thread->join ();
  }

  // Handler for messages coming in from the browser via postMessage().
  // The @a var_message can contain be any pp:Var type; for example int, string Array or Dictionary.
  // Please see the pp:Var documentation for more details.
  // @param[in] var_message The message posted by the browser.
  virtual void HandleMessage (const pp::Var& var_message) {
    // Make this function handle the incoming message.
    // Ignore the message if it is not a string.
    if (!var_message.is_string()) return;
    // Get the string message and compare it to "hello".
    string message = var_message.AsString ();
    if (message == "hello") {
      // If it matches, send our response back to JavaScript.
      pp::Var var_reply ("Hello from native Bibledit");
      PostMessage(var_reply);
    }
  }
};


// The Module class.
// The browser calls the CreateInstance() method to create an instance of your NaCl module on the web page.
// The browser creates a new instance for each <embed> tag with type="application/x-pnacl".
class BibleditModule : public pp::Module {
public:
  BibleditModule () : pp::Module () { }
  virtual ~BibleditModule () { }

  // Create and return a BibleditInstance object.
  // @param[in] instance The browser-side instance.
  // @return the plugin-side instance.
  virtual pp::Instance* CreateInstance (PP_Instance instance) {
    return new BibleditInstance (instance);
  }
};


namespace pp {
  // Factory function called by the browser when the module is first loaded.
  // The browser keeps a singleton of this module.
  // It calls the CreateInstance() method on the object you return to make instances.
  // There is one instance per <embed> tag on the page.
  // This is the main binding point for your NaCl module with the browser.
  Module* CreateModule() {
    return new BibleditModule ();
  }
}
