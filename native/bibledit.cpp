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


#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/ppp.h"
// Include the interface headers.
// PPB APIs are implemented in the "B" (browser) and describe calls from the module to the browser.
// PPP APIs are implemented in the "P" (plugin) and describe calls from the browser to the native module.
#include "ppapi/c/ppb_instance.h"
#include "ppapi/c/ppb_input_event.h"
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppp_input_event.h"
#include "ppapi/c/ppp_messaging.h"


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


static PP_Instance pp_instance = NULL;
static PPB_Instance * ppb_instance = NULL;
static PPB_InputEvent * ppb_input_event = NULL;
static PPB_GetInterface ppb_get_interface = NULL;
static PPB_Messaging* ppb_messaging = NULL;
static PPB_Var* ppb_var = NULL;
thread * bibledit_worker_thread;


// Post a message to JavaScript.
static void PostMessage (const string& message)
{
  struct PP_Var var = ppb_var->VarFromUtf8 (message.c_str (), message.size ());
  ppb_messaging->PostMessage (pp_instance, var);
  ppb_var->Release (var);
}


static PP_Bool Instance_DidCreate (PP_Instance instance, uint32_t argc, const char* argn [], const char* argv [])
{
  pp_instance = instance;
  return PP_TRUE;
}


static void Instance_DidDestroy (PP_Instance instance)
{
  // Never called.
}


static void Instance_DidChangeView (PP_Instance instance, PP_Resource view_resource)
{
  // Called right after instance creation.
}


static void Instance_DidChangeFocus (PP_Instance instance, PP_Bool has_focus)
{
  // Never called.
}


static PP_Bool Instance_HandleDocumentLoad (PP_Instance instance, PP_Resource url_loader)
{
  // Never called.
  return PP_FALSE;
}


static void Messaging_HandleMessage (PP_Instance instance, struct PP_Var message)
{
  uint32_t length;
  const char* str = ppb_var->VarToUtf8 (message, &length);
  if (str == NULL) {
    return;
  }
  // Newly allocated $new_str.
  // $str is NOT NULL-terminated. Copy using memcpy.
  char* new_str = (char *) malloc (length + 1);
  memcpy (new_str, str, length);
  new_str [length] = 0;
 
  PostMessage (new_str);
  free (new_str);
}


// Define PPP_GetInterface.
// This function should return a non-NULL value for every interface the app uses.
// The string for the name of the interface is defined in the interface's header file.
// The browser calls this function to get pointers to the interfaces that the app module implements.
PP_EXPORT const void* PPP_GetInterface(const char* interface_name)
{
  // Create structs for each PPP interface.
  // Assign the interface functions to the data fields.
  if (strcmp (interface_name, PPP_INSTANCE_INTERFACE) == 0) {
    static PPP_Instance instance_interface = {
      &Instance_DidCreate,
      &Instance_DidDestroy,
      &Instance_DidChangeView,
      &Instance_DidChangeFocus,
      &Instance_HandleDocumentLoad
    };
    return &instance_interface;
  }
  if (strcmp(interface_name, PPP_MESSAGING_INTERFACE) == 0) {
    static PPP_Messaging messaging_interface = {
      &Messaging_HandleMessage,
    };
    return &messaging_interface;
  }
  // Return NULL for interfaces not implemented.
  return NULL;
}


// Define PPP_InitializeModule, the entry point of your module.
// Retrieve the API for the browser-side (PPB) interfaces you will use.
PP_EXPORT int32_t PPP_InitializeModule (PP_Module a_module_id, PPB_GetInterface get_browser)
{
  ppb_get_interface = get_browser;
  ppb_messaging = (PPB_Messaging *) (get_browser (PPB_MESSAGING_INTERFACE));
  ppb_var = (PPB_Var *) (get_browser (PPB_VAR_INTERFACE));
  ppb_instance = (PPB_Instance *) (get_browser (PPB_INSTANCE_INTERFACE));
  ppb_input_event = (PPB_InputEvent *) (get_browser (PPB_INPUT_EVENT_INTERFACE));
  return PP_OK;
}


PP_EXPORT void PPP_ShutdownModule ()
{
  // Never called.
}


/*
// Pointer to the file system. Is null on failure.
pp::FileSystem * pepper_file_system;
*/

/*
void pepper_file_save (const string& file_name, const string& file_contents)
{

  if (!pepper_file_system) {
    cerr << "File system is not open " << PP_ERROR_FAILED << endl;
    return;
  }
  
  pp::FileRef ref (* pepper_file_system, file_name.c_str());
  pp::FileIO file (bibledit_instance);

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
  pp::FileIO file (bibledit_instance);
  
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
                  pp::FileRef) {
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


void pepper_file_list (const string& dir_name)
{
  if (!pepper_file_system) {
    cerr << "File system is not open "  << PP_ERROR_FAILED << endl;
    return;
  }
  
  pp::FileRef ref (* pepper_file_system, dir_name.c_str());
  
  pp::CompletionCallbackFactory <pp::Instance> callback_factory (bibledit_instance);

  // Pass ref along to keep it alive.
  //ref.ReadDirectoryEntries (callback_factory.NewCallbackWithOutput (ListCallback, ref));

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
*/
 

void bibledit_worker_thread_function ()
{
  cout << "Thread start" << endl;

  /*
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
  pepper_file_system = new pp::FileSystem (bibledit_instance, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  int result = pepper_file_system->Open (10 * 1024 * 1024 * 1024, pp::BlockUntilComplete());
  if (result != PP_OK) {
    pepper_file_system = nullptr;
    cerr << "Failed to open local persistent file system with error " << result << endl;
  }
  pepper_file_save ("/filename.txt", "Contents for the text file");
  pepper_file_load ("/filename.txt");
  pepper_file_delete ("/filename.txt");
  */
  
  cout << "Thread complete" << endl;
}
