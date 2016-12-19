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


#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
#include <typeinfo>


using namespace std;


#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/module.h"
#include "ppapi/utility/completion_callback_factory.h"
#include "ppapi/cpp/directory_entry.h"
#include "ppapi/utility/threading/simple_thread.h"


#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
}
#endif


void worker_thread_function ();
void post_message_to_browser (const string& message);


// The Instance class.
// One of these exists for each instance of your NaCl module on the web page.
// The browser will ask the Module object to create a new Instance
// for each occurrence of the <embed> tag that has these attributes:
// * src="bibledit.nmf"
// * type="application/x-pnacl"
// To communicate with the browser, you must override HandleMessage () to receive messages from the browser,
// and use PostMessage() to send messages back to the browser.
// Note that this interface is asynchronous.
class BibleditInstance : public pp::Instance
{
public:
  explicit BibleditInstance (PP_Instance instance);
  virtual ~BibleditInstance ();
  virtual void HandleMessage (const pp::Var& var_message);
  void ListCallback (int32_t result, const std::vector<pp::DirectoryEntry>& entries, pp::FileRef);
  pp::SimpleThread file_thread;
};


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
