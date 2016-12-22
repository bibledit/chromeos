  // Set the document root to the persistent file system.
  string webroot = "/webroot";
  // Initialize the library.
  bibledit_initialize_library (webroot.c_str(), webroot.c_str());
  // Start it.
  bibledit_start_library ();
  // Keep running till Bibledit stops or gets interrupted.
  while (bibledit_is_running ()) { }
  // Shutdown.
  bibledit_shutdown_library ();
