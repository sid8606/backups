#include "iucv.h"
int main()
{
	int sockfd, portno, n, i;
	char buffer[BUFFER_SIZE];
	struct sockaddr_iucv serv_addr;

	/* Create a socket point */
	sockfd = socket(AF_IUCV, SOCK_STREAM, IPPROTO_IP);
	if (sockfd < 0) {
		printf("Error!!!\n");
		close(sockfd);
		return SOCKET_ERROR;
	}

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.siucv_family = AF_IUCV;
	memset(&serv_addr.siucv_user_id, ' ', 8);
	memcpy(&serv_addr.siucv_user_id, "a3560036", 8);
	memcpy(&serv_addr.siucv_name, "OPNCLOUD", 8);

        if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Error!!!\n");
		close(sockfd);
		return SOCKET_ERROR;
	}

	memcpy(buffer, "Hi Sid this a a3560036 z/vm linuxguest", BUFFER_SIZE);
	/* Send messages to server. */
	n = send(sockfd, buffer, strlen(buffer)+1,0);
	if (n < 0) {
		printf("Error!!!\n");
		close(sockfd);
		return SOCKET_ERROR;
	}
#if 0
	/* Receive messages from server, to determine what should be done next */
	bzero(buffer,BUFFER_SIZE);
	n = recv(sockfd, buffer, BUFFER_SIZE,0);
	if (n < 0) {
		close(sockfd);
		return SOCKET_ERROR;
	}
	printf("%s", buffer);
#endif
	close(sockfd);
}
