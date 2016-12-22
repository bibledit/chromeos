#include <io.h>
#include <bibledit.h>


bool config_globals_webserver_running = true;


int get_line (int sock, char *buf, int size)
{
  int i = 0;
  char c = '\0';
  int n = 0;
  while ((i < size - 1) && (c != '\n')) {
    n = recv (sock, &c, 1, 0);
    if (n > 0) {
      if (c == '\r') {
        n = recv (sock, &c, 1, MSG_PEEK);
        if ((n > 0) && (c == '\n')) recv (sock, &c, 1, 0);
        else c = '\n';
      }
      buf[i] = c;
      i++;
    }
    else
    c = '\n';
  }
  buf[i] = '\0';
  return(i);
}


// Processes a single request from a web client.
void webserver_process_request (int connfd, string clientaddress)
{
  if (config_globals_webserver_running) {
    
#define BUFFERSIZE 2048
    int bytes_read;
    char buffer [BUFFERSIZE];
    // Fix valgrind unitialized value message.
    memset (&buffer, 0, BUFFERSIZE);
    do {
      bytes_read = get_line (connfd, buffer, BUFFERSIZE);
      cout << buffer << endl;
    } while (false);
    
    const char * output = "This is the response";
    // The C function strlen () fails on null characters in the reply, so take string::size()
    size_t length = strlen (output);
    send(connfd, output, length, 0);
  }
  
  shutdown (connfd, SHUT_RDWR);
  close (connfd);
}


void http_server ()
{
  bool listener_healthy = true;

  // Create a listening socket.
  // This represents an endpoint.
  // This prepares to accept incoming connections on.

  // A client listens on IPv4, see also below.
  int listenfd = socket (AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    string error = "Error opening socket: ";
    error.append (strerror (errno));
    cerr << error << endl;
    listener_healthy = false;
  }

  // Eliminate "Address already in use" error from bind.
  // The function is used to allow the local address to  be reused
  // when the server is restarted before the required wait time expires.
  int optval = 1;
  int result = setsockopt (listenfd, SOL_SOCKET, SO_REUSEADDR, (const char *) &optval, sizeof (int));
  if (result != 0) {
    string error = "Error setting socket option: ";
    error.append (strerror (errno));
    cerr << error << endl;
  }

  // The listening socket will be an endpoint for all requests to a port on this host.
  // When configured as a client, it listens on the IPv4 loopback device.
  struct sockaddr_in serveraddr;
  memset (&serveraddr, 0, sizeof (serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  serveraddr.sin_port = htons (8080);
  result = mybind (listenfd, (struct sockaddr *) &serveraddr, sizeof (serveraddr));
  if (result != 0) {
    string error = "Error binding server to socket: ";
    error.append (strerror (errno));
    cerr << error << endl;
    listener_healthy = false;
  }

  // Make it a listening socket ready to queue and accept many connection requests
  // before the system starts rejecting the incoming requests.
  result = listen (listenfd, 100);
  if (result != 0) {
    string error = "Error listening on socket: ";
    error.append (strerror (errno));
    cerr << error << endl;
    listener_healthy = false;
  }

  // Keep waiting for, accepting, and processing connections.
  while (listener_healthy && config_globals_webserver_running) {

    // Socket and file descriptor for the client connection.
    struct sockaddr_in6 clientaddr6;
    socklen_t clientlen = sizeof (clientaddr6);
    int connfd = accept (listenfd, (struct sockaddr *)&clientaddr6, &clientlen);
    if (connfd > 0) {

      // Socket receive timeout, plain http.
      struct timeval tv;
      tv.tv_sec = 60;
      tv.tv_usec = 0;
      setsockopt (connfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      
      // The client's remote IPv6 address in hexadecimal digits separated by colons.
      // IPv4 addresses are mapped to IPv6 addresses.
      string clientaddress = "::1";
      
      // Handle this request in a thread, enabling parallel requests.
      thread request_thread = thread (webserver_process_request, connfd, clientaddress);
      // Detach and delete thread object.
      request_thread.detach ();
      
    } else {
      string error = "Error accepting connection on socket: ";
      error.append (strerror (errno));
      cerr << error << endl;
    }
  }
  
  // Close listening socket, freeing it for any next server process.
  close (listenfd);
}


