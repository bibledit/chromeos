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


//#define __STDC_LIMIT_MACROS


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



#ifndef INT32_MAX
#define INT32_MAX (0x7FFFFFFF)
#endif


pp::Instance * pepper_instance;
pp::FileSystem * pepper_file_system; // Null on failure.
thread * main_worker_thread;


void pepper_file_save (const string& file_name, const string& file_contents)
{
  
  if (!pepper_file_system) {
    cerr << "File system is not open " << PP_ERROR_FAILED << endl;
    return;
  }
  
  pp::FileRef ref (* pepper_file_system, file_name.c_str());
  pp::FileIO file (pepper_instance);
  
  int32_t open_result = file.Open (ref, PP_FILEOPENFLAG_WRITE | PP_FILEOPENFLAG_CREATE | PP_FILEOPENFLAG_TRUNCATE, pp::BlockUntilComplete());
  if (open_result != PP_OK) {
    cerr << "File open for write failed " << open_result << endl;
    return;
  }
  
  // We have truncated the file to 0 bytes. So we need only write if file_contents is non-empty.
  if (!file_contents.empty()) {
    if (file_contents.length() > INT32_MAX) {
      cerr << "File too big " << PP_ERROR_FILETOOBIG << endl;
      return;
    }
    int64_t offset = 0;
    int32_t bytes_written = 0;
    do {
      bytes_written = file.Write (offset, file_contents.data() + offset, file_contents.length(), pp::BlockUntilComplete());
      if (bytes_written > 0) {
        offset += bytes_written;
      } else {
        cerr << "File write failed " << bytes_written << endl;
        return;
      }
    } while (bytes_written < static_cast<int64_t>(file_contents.length()));
  }
  
  // All bytes have been written, flush the write buffer to complete
  int32_t flush_result = file.Flush (pp::BlockUntilComplete());
  if (flush_result != PP_OK) {
    cerr << "File fail to flush " << flush_result << endl;
    return;
  }
  cout << "Save success" << endl;
}


void pepper_file_load (const string& file_name)
{
  
  if (!pepper_file_system) {
    cerr << "File system is not open "  << PP_ERROR_FAILED << endl;
    return;
  }
  
  pp::FileRef ref (* pepper_file_system, file_name.c_str());
  pp::FileIO file (pepper_instance);
  
  int32_t open_result = file.Open (ref, PP_FILEOPENFLAG_READ, pp::BlockUntilComplete());
  if (open_result == PP_ERROR_FILENOTFOUND) {
    cerr <<  "File not found " << open_result << endl;
    return;
  } else if (open_result != PP_OK) {
    cerr <<  "File open for read failed " << open_result << endl;
    return;
  }
  PP_FileInfo info;
  int32_t query_result = file.Query (&info, pp::BlockUntilComplete ());
  if (query_result != PP_OK) {
    cerr << "File query failed " << query_result << endl;
    return;
  }
  // FileIO.Read() can only handle int32 sizes
  if (info.size > INT32_MAX) {
    cerr << "File too big " << PP_ERROR_FILETOOBIG << endl;
    return;
  }
  
  std::vector<char> data (info.size);
  int64_t offset = 0;
  int32_t bytes_read = 0;
  int32_t bytes_to_read = info.size;
  while (bytes_to_read > 0) {
    bytes_read = file.Read (offset, &data[offset], data.size() - offset, pp::BlockUntilComplete());
    if (bytes_read > 0) {
      offset += bytes_read;
      bytes_to_read -= bytes_read;
    } else if (bytes_read < 0) {
      // If bytes_read < PP_OK then it indicates the error code.
      cerr << "File read failed " << bytes_read << endl;
      return;
    }
  }
  // Done reading, send content to the user interface.
  string string_data (data.begin(), data.end());
  cout << string_data << endl;
  cout << "Load success" << endl;
}


void pepper_file_delete (const string& file_name)
{
  if (!pepper_file_system) {
    cerr << "File system is not open "  << PP_ERROR_FAILED << endl;
    return;
  }
  pp::FileRef ref (* pepper_file_system, file_name.c_str());
  int32_t result = ref.Delete(pp::BlockUntilComplete());
  if (result == PP_ERROR_FILENOTFOUND) {
    cerr << "File or directory not found" << endl;
    return;
  } else if (result != PP_OK) {
    cerr << "Deletion failed " << result << endl;
    return;
  }
  cout << "Delete success" << endl;
}


void ListCallback(int32_t result,
                  const std::vector<pp::DirectoryEntry>& entries,
                  pp::FileRef /* unused_ref */) {
  if (result != PP_OK) {
    cerr << "List failed " << result << endl;
    return;
  }
  
  vector <string> sv;
  for (size_t i = 0; i < entries.size(); ++i) {
    pp::Var name = entries[i].file_ref().GetName();
    if (name.is_string()) {
      sv.push_back(name.AsString());
      cout << name.AsString() << endl;
    }
  }
  //PostArrayMessage("LIST", sv);
  cout << "List success" << endl;
}


void pepper_file_make_dir (const string& dir_name)
{
  if (!pepper_file_system) {
    cerr << "File system is not open "  << PP_ERROR_FAILED << endl;
    return;
  }
  pp::FileRef ref (* pepper_file_system, dir_name.c_str());
  
  int32_t result = ref.MakeDirectory (PP_MAKEDIRECTORYFLAG_NONE, pp::BlockUntilComplete());
  if (result != PP_OK) {
    cerr << "Make directory failed " << result << endl;
    return;
  }
  cout << "Make directory success" << endl;
}


void pepper_file_rename (const string& old_name, const string& new_name)
{
  if (!pepper_file_system) {
    cerr << "File system is not open "  << PP_ERROR_FAILED << endl;
    return;
  }
  
  pp::FileRef ref_old (* pepper_file_system, old_name.c_str ());
  pp::FileRef ref_new (* pepper_file_system, new_name.c_str());
  
  int32_t result = ref_old.Rename (ref_new, pp::BlockUntilComplete());
  if (result != PP_OK) {
    cerr << "Rename failed " << result << endl;
    return;
  }
  cout << "Rename success" << endl;
}


struct PP_CompletionCallback callback;


void pepper_file_list (const string& dir_name)
{
  if (!pepper_file_system) {
    cerr << "File system is not open "  << PP_ERROR_FAILED << endl;
    return;
  }
  
  pp::FileRef ref (* pepper_file_system, dir_name.c_str());
  
  pp::CompletionCallbackFactory <pp::Instance> callback_factory (pepper_instance);
  
  // Pass ref along to keep it alive.
  //ref.ReadDirectoryEntries (callback_factory.NewCallbackWithOutput (ListCallback, ref));
  
}


void main_worker_thread_function ()
{
  cout << "Thread start" << endl;
  
  // Open the file system on this thread.
  // Since this is the first operation we perform there,
  // and because we do everything on this thread synchronously,
  // this ensures that the FileSystem is open before any FileIO operations execute.
  // Request 10 Gbyte of space in the persistent local filesystem.
  // The Chrome app's manifest requests the "unlimitedStorage" permission already.
  // Google Chrome stores its persistent files in this folder:
  // ~/Library/Application\ Support/Google/Chrome/Default/Storage/ext/<extension-identifier>/def/File\ System/primary
  // File names for operations on this file system should start with a slash ("/").
  // Run $ du -chs to find total disk usage.
  pepper_file_system = new pp::FileSystem (pepper_instance, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  int result = pepper_file_system->Open (10 * 1024 * 1024 * 1024, pp::BlockUntilComplete());
  if (result != PP_OK) {
    pepper_file_system = nullptr;
    cerr << "Failed to open local persistent file system with error " << result << endl;
  }
  pepper_file_save ("/filename.txt", "Contents for the text file");
  pepper_file_load ("/filename.txt");
  pepper_file_list ("/");
  pepper_file_delete ("/filename.txt");
  
  
  cout << "Thread complete" << endl;
}


/*
The Instance class.
One of these exists for each instance of your NaCl module on the web page.
The browser will ask the Module object to create a new Instance
for each occurrence of the <embed> tag that has these attributes:
* src="bibledit.nmf"
* type="application/x-pnacl"
To communicate with the browser, you must override HandleMessage () to receive messages from the browser,
and use PostMessage() to send messages back to the browser.
Note that this interface is asynchronous.
*/
class BibleditInstance : public pp::Instance
{
public:
  // The constructor creates the plugin-side instance.
  // @param[in] instance the handle to the browser-side plugin instance.
  explicit BibleditInstance (PP_Instance instance) : pp::Instance (instance)
  {
    pepper_instance = this;
    main_worker_thread = new thread (main_worker_thread_function);
  }
  
  virtual ~BibleditInstance ()
  {
    main_worker_thread->join ();
  }
  
  // Handler for messages coming in from the browser via postMessage().
  // The @a var_message can contain be any pp:Var type; for example int, string Array or Dictionary.
  // Please see the pp:Var documentation for more details.
  // @param[in] var_message The message posted by the browser.
  virtual void HandleMessage (const pp::Var& var_message)
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
};


// The Module class.
// The browser calls the CreateInstance() method to create an instance of your NaCl module on the web page.
// The browser creates a new instance for each <embed> tag with type="application/x-pnacl".
class BibleditModule : public pp::Module {
public:
  BibleditModule () : pp::Module ()
  {
  }

  virtual ~BibleditModule ()
  {
  }
  
  // Create and return a BibleditInstance object.
  // @param[in] instance The browser-side instance.
  // @return the plugin-side instance.
  virtual pp::Instance* CreateInstance (PP_Instance instance)
  {
    return new BibleditInstance (instance);
  }
};


namespace pp {
  // Factory function called by the browser when the module is first loaded.
  // The browser keeps a singleton of this module.
  // It calls the CreateInstance() method on the object you return to make instances.
  // There is one instance per <embed> tag on the page.
  // This is the main binding point for your NaCl module with the browser.
  Module * CreateModule ()
  {
    return new BibleditModule ();
  }
}
