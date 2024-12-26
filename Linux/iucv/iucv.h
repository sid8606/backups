
#ifndef IUCV_H
#define IUCV_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netiucv/iucv.h>
#include <syslog.h>
#include <string.h>
#include <pthread.h>
#include<unistd.h>

#define BUFFER_SIZE	64
#define SOCKET_ERROR	(-1)
#define SOCKET_TIMEOUT   20
#endif
