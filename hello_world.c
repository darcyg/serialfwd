/*
 * hello_world.c
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;

#define PROXYPORT 2101

static void usage(char *name)
{
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s (serialportdevice|ipaddr) answerbytes [hex [hex...]]\n", name);
  fprintf(stderr, "  - sends specified hex bytes and then waits for specified number of answer bytes (-1=forever, use Ctrl-C to abort)\n");
  fprintf(stderr, "  %s (serialportdevice|ipaddr)\n", name);
  fprintf(stderr, "  - proxy mode: accepts connection on TCP port %d and forwards to/from serial/ipaddr\n", PROXYPORT);
}



int main(int argc, char **argv)
{
  if (argc<2) {
    // show usage
    usage(argv[0]);
    exit(1);
  }
  int proxyMode = FALSE;
  int serialMode = FALSE;

  if (argc==2) {
    // proxy mode
    proxyMode = TRUE;
  }

  int argIdx;
  int data;
  unsigned char byte;

  // Open input
  int outputfd =0;
  int res;
  char *outputname = argv[1];
  struct termios oldtio,newtio;

  serialMode = *(argv[1])=='/';

  // check type of input
  if (serialMode) {
    // assume it's a serial port
    outputfd = open(outputname, O_RDWR | O_NOCTTY);
    if (outputfd <0) {
      perror(outputname); exit(-1);
    }
    tcgetattr(outputfd,&oldtio); // save current port settings

    // see "man termios" for details
    memset(&newtio, 0, sizeof(newtio));
    // - baudrate, 8-N-1, no modem control lines (local), reading enabled
    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
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
    conn_addr.sin_port = htons(PROXYPORT);

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

    int n;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PROXYPORT); // port

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    listen(listenfd, 1); // max one connection for now

    printf("Proxy mode, listening on port %d for connections\n",PROXYPORT);

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
          n = read(servingfd, &byte, 1);
          if (n<1) break; // connection closed
          // got a byte, send it
          printf("Transmitting byte : 0x%02X\n", byte);
          // send
          res = write(outputfd,&byte,1);
        }
        if (FD_ISSET(outputfd,&readfs)) {
          // input from serial available
          res = read(outputfd,&byte,1);   /* returns after 1 chars have been input */
          printf("Received     byte : 0x%02X\n", byte);
          res = write(servingfd,&byte,1);
        }
      }
      close(servingfd);
      printf("Connection closed, waiting for new connection\n");
    }
	}
	else {
    // command line direct mode
	  int numRespBytes = 0;
	  sscanf(argv[2],"%d",&numRespBytes);

	  // parse and send the input bytes
    for (argIdx=3; argIdx<argc; argIdx++) {
      // parse as hex
      sscanf(argv[argIdx],"%x",&data);
      byte = data;
      // show
      printf("Transmitting byte : 0x%02X\n",data);
      // send
      res = write(outputfd,&byte,1);
    }

    while (numRespBytes<0 || numRespBytes>0) {       /* loop for input */
      res = read(outputfd,&byte,1);   /* returns after 1 chars have been input */
      printf("Received     byte : 0x%02X\n",byte);
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
