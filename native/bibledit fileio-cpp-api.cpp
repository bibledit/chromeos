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


#define __STDC_LIMIT_MACROS


#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/ppb_file_io.h"
#include "ppapi/cpp/directory_entry.h"
#include "ppapi/cpp/file_io.h"
#include "ppapi/cpp/file_ref.h"
#include "ppapi/cpp/file_system.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/message_loop.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_array.h"
#include "ppapi/utility/completion_callback_factory.h"
#include "ppapi/utility/threading/simple_thread.h"


#ifndef INT32_MAX
#define INT32_MAX (0x7FFFFFFF)
#endif


// The Instance class.
// One of these exists for each instance of your NaCl module on the web page.
// The browser will ask the Module object to create a new Instance
// for each occurrence of the <embed> tag that has these attributes:
//     type="application/x-nacl"
//     src="bibledit.nmf"
class BibleditInstance : public pp::Instance {
 public:
  // The constructor creates the plugin-side instance.
  // @param[in] instance the handle to the browser-side plugin instance.
  explicit BibleditInstance(PP_Instance instance)
      : pp::Instance(instance),
        callback_factory_(this),
        file_system_(this, PP_FILESYSTEMTYPE_LOCALPERSISTENT),
        file_system_ready_(false),
        file_thread_(this) {}

  virtual ~BibleditInstance() { file_thread_.Join(); }

  virtual bool Init(uint32_t /*argc*/,
                    const char * /*argn*/ [],
                    const char * /*argv*/ []) {
    file_thread_.Start();
    // Open the file system on the file_thread_. Since this is the first
    // operation we perform there, and because we do everything on the
    // file_thread_ synchronously, this ensures that the FileSystem is open
    // before any FileIO operations execute.
    file_thread_.message_loop().PostWork(
        callback_factory_.NewCallback(&BibleditInstance::OpenFileSystem));
    return true;
  }

 private:
  pp::CompletionCallbackFactory<BibleditInstance> callback_factory_;
  pp::FileSystem file_system_;

  // Indicates whether file_system_ was opened successfully. We only read/write
  // this on the file_thread_.
  bool file_system_ready_;

  // We do all our file operations on the file_thread_.
  pp::SimpleThread file_thread_;

  void PostArrayMessage(const char* command, const vector <string> & strings) {
    pp::VarArray message;
    message.Set(0, command);
    for (size_t i = 0; i < strings.size(); ++i) {
      message.Set(i + 1, strings[i]);
    }

    PostMessage(message);
  }

  void PostArrayMessage(const char* command) {
    PostArrayMessage(command, vector <string> ());
  }

  void PostArrayMessage(const char* command, const std::string& s) {
    vector <string> sv;
    sv.push_back(s);
    PostArrayMessage(command, sv);
  }

  // Handler for messages coming in from the browser via postMessage().  The
  // @a var_message can contain anything: a JSON string; a string that encodes
  // method names and arguments; etc.
  //
  // Here we use messages to communicate with the user interface
  //
  // @param[in] var_message The message posted by the browser.
  virtual void HandleMessage(const pp::Var& var_message) {
    if (!var_message.is_array())
      return;

    // Message should be an array with the following elements:
    // [command, path, extra args]
    pp::VarArray message(var_message);
    std::string command = message.Get(0).AsString();
    std::string file_name = message.Get(1).AsString();

    if (file_name.length() == 0 || file_name[0] != '/') {
      ShowStatusMessage("File name must begin with /");
      return;
    }

    printf("command: %s file_name: %s\n", command.c_str(), file_name.c_str());

    if (command == "load") {
      file_thread_.message_loop().PostWork(
          callback_factory_.NewCallback(&BibleditInstance::Load, file_name));
    } else if (command == "save") {
      std::string file_text = message.Get(2).AsString();
      file_thread_.message_loop().PostWork(callback_factory_.NewCallback(
          &BibleditInstance::Save, file_name, file_text));
    } else if (command == "delete") {
      file_thread_.message_loop().PostWork(
          callback_factory_.NewCallback(&BibleditInstance::Delete, file_name));
    } else if (command == "list") {
      const std::string& dir_name = file_name;
      file_thread_.message_loop().PostWork(
          callback_factory_.NewCallback(&BibleditInstance::List, dir_name));
    } else if (command == "makedir") {
      const std::string& dir_name = file_name;
      file_thread_.message_loop().PostWork(
          callback_factory_.NewCallback(&BibleditInstance::MakeDir, dir_name));
    } else if (command == "rename") {
      const std::string new_name = message.Get(2).AsString();
      file_thread_.message_loop().PostWork(callback_factory_.NewCallback(
          &BibleditInstance::Rename, file_name, new_name));
    }
  }

  void OpenFileSystem(int32_t /* result */) {
    int32_t rv = file_system_.Open(1024 * 1024, pp::BlockUntilComplete());
    if (rv == PP_OK) {
      file_system_ready_ = true;
      // Notify the user interface that we're ready
      PostArrayMessage("READY");
    } else {
      ShowErrorMessage("Failed to open file system", rv);
    }
  }

  void Save(int32_t /* result */,
            const std::string& file_name,
            const std::string& file_contents) {
    if (!file_system_ready_) {
      ShowErrorMessage("File system is not open", PP_ERROR_FAILED);
      return;
    }
    pp::FileRef ref(file_system_, file_name.c_str());
    pp::FileIO file(this);

    int32_t open_result =
        file.Open(ref,
                  PP_FILEOPENFLAG_WRITE | PP_FILEOPENFLAG_CREATE |
                      PP_FILEOPENFLAG_TRUNCATE,
                  pp::BlockUntilComplete());
    if (open_result != PP_OK) {
      ShowErrorMessage("File open for write failed", open_result);
      return;
    }

    // We have truncated the file to 0 bytes. So we need only write if
    // file_contents is non-empty.
    if (!file_contents.empty()) {
      if (file_contents.length() > INT32_MAX) {
        ShowErrorMessage("File too big", PP_ERROR_FILETOOBIG);
        return;
      }
      int64_t offset = 0;
      int32_t bytes_written = 0;
      do {
        bytes_written = file.Write(offset,
                                   file_contents.data() + offset,
                                   file_contents.length(),
                                   pp::BlockUntilComplete());
        if (bytes_written > 0) {
          offset += bytes_written;
        } else {
          ShowErrorMessage("File write failed", bytes_written);
          return;
        }
      } while (bytes_written < static_cast<int64_t>(file_contents.length()));
    }
    // All bytes have been written, flush the write buffer to complete
    int32_t flush_result = file.Flush(pp::BlockUntilComplete());
    if (flush_result != PP_OK) {
      ShowErrorMessage("File fail to flush", flush_result);
      return;
    }
    ShowStatusMessage("Save success");
  }

  void Load(int32_t /* result */, const std::string& file_name) {
    if (!file_system_ready_) {
      ShowErrorMessage("File system is not open", PP_ERROR_FAILED);
      return;
    }
    pp::FileRef ref(file_system_, file_name.c_str());
    pp::FileIO file(this);

    int32_t open_result =
        file.Open(ref, PP_FILEOPENFLAG_READ, pp::BlockUntilComplete());
    if (open_result == PP_ERROR_FILENOTFOUND) {
      ShowErrorMessage("File not found", open_result);
      return;
    } else if (open_result != PP_OK) {
      ShowErrorMessage("File open for read failed", open_result);
      return;
    }
    PP_FileInfo info;
    int32_t query_result = file.Query(&info, pp::BlockUntilComplete());
    if (query_result != PP_OK) {
      ShowErrorMessage("File query failed", query_result);
      return;
    }
    // FileIO.Read() can only handle int32 sizes
    if (info.size > INT32_MAX) {
      ShowErrorMessage("File too big", PP_ERROR_FILETOOBIG);
      return;
    }

    std::vector<char> data(info.size);
    int64_t offset = 0;
    int32_t bytes_read = 0;
    int32_t bytes_to_read = info.size;
    while (bytes_to_read > 0) {
      bytes_read = file.Read(offset,
                             &data[offset],
                             data.size() - offset,
                             pp::BlockUntilComplete());
      if (bytes_read > 0) {
        offset += bytes_read;
        bytes_to_read -= bytes_read;
      } else if (bytes_read < 0) {
        // If bytes_read < PP_OK then it indicates the error code.
        ShowErrorMessage("File read failed", bytes_read);
        return;
      }
    }
    // Done reading, send content to the user interface
    std::string string_data(data.begin(), data.end());
    PostArrayMessage("DISP", string_data);
    ShowStatusMessage("Load success");
  }

  void Delete(int32_t /* result */, const std::string& file_name) {
    if (!file_system_ready_) {
      ShowErrorMessage("File system is not open", PP_ERROR_FAILED);
      return;
    }
    pp::FileRef ref(file_system_, file_name.c_str());

    int32_t result = ref.Delete(pp::BlockUntilComplete());
    if (result == PP_ERROR_FILENOTFOUND) {
      ShowStatusMessage("File/Directory not found");
      return;
    } else if (result != PP_OK) {
      ShowErrorMessage("Deletion failed", result);
      return;
    }
    ShowStatusMessage("Delete success");
  }

  void List(int32_t /* result */, const std::string& dir_name) {
    if (!file_system_ready_) {
      ShowErrorMessage("File system is not open", PP_ERROR_FAILED);
      return;
    }

    pp::FileRef ref(file_system_, dir_name.c_str());

    // Pass ref along to keep it alive.
    ref.ReadDirectoryEntries(callback_factory_.NewCallbackWithOutput(
        &BibleditInstance::ListCallback, ref));
  }

  void ListCallback(int32_t result,
                    const std::vector<pp::DirectoryEntry>& entries,
                    pp::FileRef /* unused_ref */) {
    if (result != PP_OK) {
      ShowErrorMessage("List failed", result);
      return;
    }

    vector <string> sv;
    for (size_t i = 0; i < entries.size(); ++i) {
      pp::Var name = entries[i].file_ref().GetName();
      if (name.is_string()) {
        sv.push_back(name.AsString());
      }
    }
    PostArrayMessage("LIST", sv);
    ShowStatusMessage("List success");
  }

  void MakeDir(int32_t /* result */, const std::string& dir_name) {
    if (!file_system_ready_) {
      ShowErrorMessage("File system is not open", PP_ERROR_FAILED);
      return;
    }
    pp::FileRef ref(file_system_, dir_name.c_str());

    int32_t result = ref.MakeDirectory(
        PP_MAKEDIRECTORYFLAG_NONE, pp::BlockUntilComplete());
    if (result != PP_OK) {
      ShowErrorMessage("Make directory failed", result);
      return;
    }
    ShowStatusMessage("Make directory success");
  }

  void Rename(int32_t /* result */,
              const std::string& old_name,
              const std::string& new_name) {
    if (!file_system_ready_) {
      ShowErrorMessage("File system is not open", PP_ERROR_FAILED);
      return;
    }

    pp::FileRef ref_old(file_system_, old_name.c_str());
    pp::FileRef ref_new(file_system_, new_name.c_str());

    int32_t result = ref_old.Rename(ref_new, pp::BlockUntilComplete());
    if (result != PP_OK) {
      ShowErrorMessage("Rename failed", result);
      return;
    }
    ShowStatusMessage("Rename success");
  }

  // Encapsulates our simple javascript communication protocol
  void ShowErrorMessage(const std::string& message, int32_t result) {
    std::stringstream ss;
    ss << message << " -- Error #: " << result;
    PostArrayMessage("ERR", ss.str());
  }

  void ShowStatusMessage(const std::string& message) {
    PostArrayMessage("STAT", message);
  }
};

// The Module class.  The browser calls the CreateInstance() method to create
// an instance of your NaCl module on the web page.  The browser creates a new
// instance for each <embed> tag with type="application/x-nacl".
class BibleditModule : public pp::Module {
 public:
  BibleditModule() : pp::Module() {}
  virtual ~BibleditModule() {}

  // Create and return a BibleditInstance object.
  // @param[in] instance The browser-side instance.
  // @return the plugin-side instance.
  virtual pp::Instance* CreateInstance (PP_Instance instance) {
    return new BibleditInstance(instance);
  }
};

namespace pp {
// Factory function called by the browser when the module is first loaded.
// The browser keeps a singleton of this module.  It calls the
// CreateInstance() method on the object you return to make instances.  There
// is one instance per <embed> tag on the page.  This is the main binding
// point for your NaCl module with the browser.
Module* CreateModule() { return new BibleditModule(); }
}  // namespace pp
