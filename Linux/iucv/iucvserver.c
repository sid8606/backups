#include "iucv.h"
int main()
{
	int sockfd, newsockfd, portno, clilen;
	char send_buf[BUFFER_SIZE], buffer[BUFFER_SIZE];
	struct sockaddr_iucv cli_addr;
	struct sockaddr_iucv serv_addr;
	size_t n = 0, on=1;

	/* First call to socket() function */
	sockfd = socket(AF_IUCV, SOCK_STREAM, 0);
	if (sockfd < 0) {
		printf("Error!!!\n");
		syslog(LOG_ERR, "ERROR opening socket: %s\n",strerror(errno));
		close(sockfd);
		return errno;
	}
	if ((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))<0) {
		printf("Error!!!\n");
		syslog(LOG_ERR, "ERROR setsockopt: %s\n",strerror(errno));
		close(sockfd);
		return errno;
	}

	/* Initialize socket structure */
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.siucv_family = AF_IUCV;
	memcpy(serv_addr.siucv_name, "OPNCLOUD", 8);
	memcpy(serv_addr.siucv_user_id, "a3560034", 8);

	/* Now bind the host address using bind() call.*/
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		printf("Error!!!\n");
		syslog(LOG_ERR, "ERROR on binding: (errno %d) %s\n", errno, strerror(errno));
		close(sockfd);
		return errno;
	}
	/* Now start listening for the clients, here process will
	 * go in sleep mode and will wait for the incoming connection
	 */
	if (listen(sockfd,SOCKET_TIMEOUT) < 0) {
		printf("Error!!!\n");
		syslog(LOG_ERR, "ERROR on liston: %s\n",strerror(errno));
		close(sockfd);
		return errno;
	}
	clilen = sizeof(cli_addr);    /* Initialize socket structure */

	while (1) {
		/* Accept actual connection from the client */
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (newsockfd < 0) {
			printf("Error!!!\n");
			syslog(LOG_ERR, "ERROR on accepting: %s\n",strerror(errno));
			close(newsockfd);
			close(sockfd);
			return errno;
		}
		/* If connection is established then start communicating */
		bzero(buffer, BUFFER_SIZE);
		n =  recv(newsockfd, buffer, BUFFER_SIZE, 0);
		if (n < 0) {
			printf("Error!!!\n");
			syslog(LOG_ERR, "ERROR reading from socket: %s\n",strerror(errno));
			close(newsockfd);
			close(sockfd);
			return errno;
		}

		printf("%s", buffer);
		close(newsockfd);
	}
	close(sockfd);
}
