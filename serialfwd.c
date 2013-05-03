/*
 * serialforwarder.c
 *
 * Description: Serial test
 *
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/param.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;

#define DEFAULT_PROXYPORT 2101
#define DEFAULT_CONNECTIONPORT 2101
#define DEFAULT_BAUDRATE 9600


static void usage(char *name)
{
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s [-P port] (serialportdevice|ipaddr) answerbytes [hex [hex...]]\n", name);
  fprintf(stderr, "    sends specified hex bytes and then waits for specified number of answer bytes\n");
  fprintf(stderr, "    (answerbytes == INF: wait forever, use Ctrl-C to abort)\n");
  fprintf(stderr, "    -P port : port to connect to (default: %d)\n", DEFAULT_CONNECTIONPORT);
  fprintf(stderr, "  %s [-P port] (serialportdevice|ipaddr) [-d] [-p port]\n", name);
  fprintf(stderr, "    proxy mode: accepts TCP connection and forwards to/from serial/ipaddr\n");
  fprintf(stderr, "    -d : fully daemonize and suppress showing byte transfer messages on stdout\n");
  fprintf(stderr, "    -p port : port to accept connections from (default: %d)\n", DEFAULT_PROXYPORT);
  fprintf(stderr, "    -P port : port to connect to (default: %d)\n", DEFAULT_CONNECTIONPORT);
  fprintf(stderr, "    -b baudrate : baudrate when connecting to serial port (default: %d)\n", DEFAULT_BAUDRATE);
}


static void daemonize(void)
{
  pid_t pid, sid;

  /* already a daemon */
  if ( getppid() == 1 ) return;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* At this point we are executing as the child process */

  /* Change the file mode mask */
  umask(0);

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  /* Change the current working directory.  This prevents the current
     directory from being locked; hence not being able to remove it. */
  if ((chdir("/")) < 0) {
    exit(EXIT_FAILURE);
  }

  /* Redirect standard files to /dev/null */
  freopen( "/dev/null", "r", stdin);
  freopen( "/dev/null", "w", stdout);
  freopen( "/dev/null", "w", stderr);
}



int main(int argc, char **argv)
{
  if (argc<2) {
    // show usage
    usage(argv[0]);
    exit(1);
  }
  int proxyMode = FALSE;
  int daemonMode = FALSE;
  int serialMode = FALSE;
  int verbose = TRUE;
  int proxyPort = DEFAULT_PROXYPORT;
  int connPort = DEFAULT_CONNECTIONPORT;
  int baudRate = DEFAULT_BAUDRATE;
  int baudRateCode = B0;

  int c;
  while ((c = getopt(argc, argv, "hdp:P:b:")) != -1)
  {
    switch (c) {
      case 'h':
        usage(argv[0]);
        exit(0);
      case 'd':
        daemonMode = TRUE;
        verbose = FALSE;
        break;
      case 'p':
        proxyPort = atoi(optarg);
        break;
      case 'P':
        connPort = atoi(optarg);
        break;
      case 'b':
        baudRate = atoi(optarg);
        break;
      default:
        exit(-1);
    }
  }
  // proxymode is when only one arg is here
  if (argc-optind == 1 || daemonMode) {
    proxyMode = TRUE;
  }

  // daemonize now if requested and in proxy mode
  if (daemonMode && proxyMode) {
    printf("Starting background daemon listening on port %d for connections\n",proxyPort);
    daemonize();
  }


  int argIdx;
  int data;
  unsigned char byte;

  // Open input
  int outputfd =0;
  int res;
  char *outputname = argv[optind++];
  struct termios oldtio,newtio;

  serialMode = *outputname=='/';

  // check type of input
  if (serialMode) {
    // assume it's a serial port
    switch (baudRate) {
      case 50 : baudRateCode = B50; break;
      case 75 : baudRateCode = B75; break;
      case 134 : baudRateCode = B134; break;
      case 150 : baudRateCode = B150; break;
      case 200 : baudRateCode = B200; break;
      case 300 : baudRateCode = B300; break;
      case 600 : baudRateCode = B600; break;
      case 1200 : baudRateCode = B1200; break;
      case 1800 : baudRateCode = B1800; break;
      case 2400 : baudRateCode = B2400; break;
      case 4800 : baudRateCode = B4800; break;
      case 9600 : baudRateCode = B9600; break;
      case 19200 : baudRateCode = B19200; break;
      case 38400 : baudRateCode = B38400; break;
      case 57600 : baudRateCode = B57600; break;
      case 115200 : baudRateCode = B115200; break;
      case 230400 : baudRateCode = B230400; break;
      #ifndef __APPLE__
      case 460800 : baudRateCode = B460800; break;
      #endif

      default:
        fprintf(stderr, "invalid baudrate %d (standard baudrates 50..460800 are supported)\n", baudRate);
        exit(1);
    }

    outputfd = open(outputname, O_RDWR | O_NOCTTY);
    if (outputfd <0) {
      perror(outputname); exit(-1);
    }
    tcgetattr(outputfd,&oldtio); // save current port settings

    // see "man termios" for details
    memset(&newtio, 0, sizeof(newtio));
    // - baudrate, 8-N-1, no modem control lines (local), reading enabled
    newtio.c_cflag = baudRateCode | CRTSCTS | CS8 | CLOCAL | CREAD;
    // - ignore parity errors
    newtio.c_iflag = IGNPAR;
    // - no output control
    newtio.c_oflag = 0;
    // - no input control (non-canonical)
    newtio.c_lflag = 0;
    // - no inter-char time
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    // - receive every single char seperately
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 chars received */
    // - set new params
    tcflush(outputfd, TCIFLUSH);
    tcsetattr(outputfd,TCSANOW,&newtio);
  }
  else {
    // assume it's an IP address or hostname
    struct sockaddr_in conn_addr;
    if ((outputfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      printf("Error: Could not create socket\n");
      exit(1);
    }
    // prepare IP address
    memset(&conn_addr, '0', sizeof(conn_addr));
    conn_addr.sin_family = AF_INET;
    conn_addr.sin_port = htons(connPort);

    struct hostent *server;
    server = gethostbyname(outputname);
    if (server == NULL) {
      printf("Error: no such host");
      exit(1);
    }
    memcpy((char *)&conn_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);

    if ((res = connect(outputfd, (struct sockaddr *)&conn_addr, sizeof(conn_addr))) < 0) {
      printf("Error: %s\n", strerror(errno));
      exit(1);
    }
  }

  if (proxyMode) {

    int listenfd = 0, servingfd = 0;
    struct sockaddr_in serv_addr;

    fd_set readfs;    /* file descriptor set */
    int    maxrdfd;     /* maximum file descriptor used */

    const size_t bufsiz = 200;
    unsigned char buffer[bufsiz];
    size_t numBytes;
    size_t gotBytes;
    size_t i;

    int n;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(proxyPort); // port

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    listen(listenfd, 1); // max one connection for now

    if (verbose) printf("Proxy mode, listening on port %d for connections\n",proxyPort);

    while (TRUE) {
      // accept the connection, open fd
      servingfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
      // prepare fd observation using select()
      maxrdfd = MAX (outputfd, servingfd)+1;  /* maximum bit entry (fd) to test */
      FD_ZERO(&readfs);
      // wait for getting data from either side now
      while (TRUE) {
        FD_SET(servingfd, &readfs);  /* set testing for source 2 */
        FD_SET(outputfd, &readfs);  /* set testing for source 1 */
        // block until input becomes available
        select(maxrdfd, &readfs, NULL, NULL, NULL);
        if (FD_ISSET(servingfd,&readfs)) {
          // input from TCP connection available
          // - get number of bytes available
          n = ioctl(servingfd, FIONREAD, &numBytes);
          if (n<0) break; // connection closed
          // limit to max buffer size
          if (numBytes>bufsiz)
            numBytes = bufsiz;
          // read
          gotBytes = 0;
          if (numBytes>0)
            gotBytes = read(servingfd,buffer,numBytes); // read available bytes
          if (gotBytes<1) break; // connection closed
          // got bytes, send them
          if (verbose) {
            printf("Transmitting : ");
            for (i=0; i<gotBytes; ++i) {
              printf("0x%02X ", buffer[i]);
            }
            printf("\n");
          }
          // send
          res = write(outputfd,buffer,gotBytes);
        }
        if (FD_ISSET(outputfd,&readfs)) {
          // input from serial available
          // - get number of bytes available
          n = ioctl(outputfd, FIONREAD, &numBytes);
          if (n<0) break; // connection closed
          // limit to max buffer size
          if (numBytes>bufsiz)
            numBytes = bufsiz;
          // read
          gotBytes = 0;
          if (numBytes>0)
            gotBytes = read(outputfd,buffer,numBytes); // read available bytes
          if (gotBytes<1) break; // connection closed
          // got bytes, send them
          if (verbose) {
            printf("Received     : ");
            for (i=0; i<gotBytes; ++i) {
              printf("0x%02X ", buffer[i]);
            }
            printf("\n");
          }
          // send
          res = write(servingfd,buffer,gotBytes);
        }
      }
      close(servingfd);
      if (verbose) printf("Connection closed, waiting for new connection\n");
    }
  }
  else {
    // command line direct mode
    int numRespBytes = 0;
    if (strcmp(argv[optind],"INF")==0) {
      // wait indefinitely
      numRespBytes = -1;
    }
    else {
      // parse number of bytes expected
      sscanf(argv[optind],"%d",&numRespBytes);
    }
    optind++;
    // parse and send the input bytes
    for (argIdx=optind; argIdx<argc; argIdx++) {
      // parse as hex
      sscanf(argv[argIdx],"%x",&data);
      byte = data;
      // show
      if (verbose) printf("Transmitting byte : 0x%02X\n",data);
      // send
      res = write(outputfd,&byte,1);
    }

    while (numRespBytes<0 || numRespBytes>0) {       /* loop for input */
      res = read(outputfd,&byte,1);   /* returns after 1 chars have been input */
      if (verbose) printf("Received     byte : 0x%02X\n",byte);
      numRespBytes--;
    }
  }

  // done
  if (serialMode) {
    tcsetattr(outputfd,TCSANOW,&oldtio);
  }

  // close
  close(outputfd);

  // return
  return 0;
}
