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


pp::CompletionCallbackFactory <BibleditInstance> * callback_factory = nullptr;


class BibleditInstance : public pp::Instance
{
public:
  explicit BibleditInstance (PP_Instance instance);
  virtual ~BibleditInstance ();
  virtual void HandleMessage (const pp::Var& var_message);
  void ListCallback (int32_t result, const std::vector<pp::DirectoryEntry>& entries, pp::FileRef);
  pp::SimpleThread file_thread;
};


class BibleditModule : public pp::Module
{
public:
  BibleditModule ();
  virtual ~BibleditModule ();
  virtual pp::Instance* CreateInstance (PP_Instance instance);
};
