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
#include "ppapi/c/ppp.h"
#include "ppapi/c/ppb.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/instance.h"
#include "nacl_io/ioctl.h"
#include "nacl_io/nacl_io.h"
#include "nacl_io/osdirent.h"
#include "nacl_io/osinttypes.h"


#define __STDC_LIMIT_MACROS


#ifndef INT32_MAX
#define INT32_MAX (0x7FFFFFFF)
#endif


pp::Instance * pepper_instance = nullptr;
pp::Module * pepper_module = nullptr;
thread * main_worker_thread = nullptr;


vector <string> filter_url_scandir_internal (string folder)
{
  vector <string> files;
  
  DIR * dir = opendir (folder.c_str());
  if (dir) {
    struct dirent * direntry;
    while ((direntry = readdir (dir)) != NULL) {
      string name = direntry->d_name;
      if (name.substr (0, 1) == ".") continue;
      files.push_back (name);
      post_message_to_gui ("Reading file " + name);
    }
    closedir (dir);
  }
  sort (files.begin(), files.end());
  
  // Remove . and ..
  //files = filter_string_array_diff (files, {".", ".."});
  
  return files;
}


vector <string> filter_url_scandir (string folder)
{
  vector <string> files = filter_url_scandir_internal (folder);
  //files = filter_string_array_diff (files, {"gitflag"});
  return files;
}


string filter_url_file_get_contents (string filename)
{
  //if (!file_or_dir_exists(filename)) return "";
  try {
    ifstream ifs(filename.c_str(), ios::in | ios::binary | ios::ate);
    streamoff filesize = ifs.tellg();
    if (filesize == 0) return "";
    ifs.seekg(0, ios::beg);
    vector <char> bytes((int)filesize);
    ifs.read(&bytes[0], (int)filesize);
    return string(&bytes[0], (int)filesize);
  }
  catch (...) {
    return "";
  }
}


void filter_url_file_put_contents (string filename, string contents)
{
  try {
    ofstream file;
    file.open(filename, ios::binary | ios::trunc);
    file << contents;
    file.close ();
  } catch (...) {
  }
}


void initialize_filesystem ()
{
  // Initialize nacl_io with PPAPI support.
  nacl_io_init_ppapi (pepper_instance->pp_instance(), pepper_module->Get ()->get_browser_interface());
  
  // By default, nacl_io mounts "/" to pass through to the original NaCl filesystem.
  // That doesn't do much.
  // Remount it to a memfs filesystem.
  int result = umount ("/");
  if (result != 0) {
    cerr << strerror (errno) << endl;
  }

  result = mount ("", "/", "memfs", 0, ""); // Todo for /tmp for extra speed.
  if (result != 0) {
    cerr << strerror (errno) << endl;
  }

  mkdir ("/persistent", 0777);
  result = mount ("",                                           // source
                  "/persistent",                                // target
                  "html5fs",                                    // filesystemtype
                  0,                                            // mountflags
                  "type=PERSISTENT,expected_size=10737418240"); // data
  if (result != 0) {
    cerr << strerror (errno) << endl;
  }
  
  result = mount ("",       /* source. Use relative URL */
                  "/http",  /* target */
                  "httpfs", /* filesystemtype */
                  0,        /* mountflags */
                  "");      /* data */
  if (result != 0) {
    cerr << strerror (errno) << endl;
  }

}


void main_worker_thread_function ()
{
  cout << "Thread start" << endl;
  
  initialize_filesystem ();
  
  string directory = "/persistent";
  //directory = "/";
  for (unsigned int i = 0; i < 10; i++) {
    string file = directory + "/file" + to_string (i) + ".txt";
    filter_url_file_put_contents (file, string (1024 * 1024, 'X'));
    string contents = filter_url_file_get_contents (file);
    post_message_to_gui (file + ": " + to_string (contents.size ()));
    
  }
  
  vector <string> files = filter_url_scandir (directory);
  for (auto file : files) cout << file << endl;

  
  // Remove interception for POSIX C-library function and release associated resources.
  nacl_io_uninit ();

  cout << "Thread complete" << endl;
}


class BibleditInstance : public pp::Instance
{
public:
  explicit BibleditInstance (PP_Instance instance);
  virtual ~BibleditInstance ();
  virtual void HandleMessage (const pp::Var& var_message);
};


class BibleditModule : public pp::Module
{
public:
  BibleditModule ();
  virtual ~BibleditModule ();
  virtual pp::Instance* CreateInstance (PP_Instance instance);
};


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
    pp::Var var_reply ("Hello from native Bibledit");
    PostMessage (var_reply);
  }
}


// The Module class.
// The browser calls the CreateInstance() method to create an instance of your NaCl module on the web page.
// The browser creates a new instance for each <embed> tag with type="application/x-pnacl".
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


void post_message_to_gui (const string & msg)
{
  pp::Var var_reply (msg);
  pepper_instance->PostMessage (var_reply);
}
