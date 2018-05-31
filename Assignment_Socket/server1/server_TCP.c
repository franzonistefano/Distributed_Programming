#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <string.h>
#include <time.h>

#include "../errlib.h"
#include "../sockwrap.h"

#define LISTENQ 15
#define MAXBUFL 255
#define MAX_STR 1023
#define CHUNK_SIZE 1024

#ifdef TRACE
#define trace(x) x
#else
#define trace(x)
#endif

#define MSG_ERR "-ERR\r\n"
#define MSG_OK "+OK\r\n"
#define MSG_QUIT "QUIT"
#define MSG_GET "GET"

char *prog_name;

int receiver (int connfd) {

	char buf[MAXBUFL+1]; /* +1 to make room for \0 */
	int res;
	int nread, ret;
	int ret_val = 0;
	struct stat info;

	while (1) {
		printf("[+] Waiting for commands ...\n");
		trace( err_msg("(%s) - waiting for commands ...",prog_name) );
		nread = 0;
		nread = Recv(connfd, buf, MAXBUFL, 0);
		printf("[+] Received %s\n", buf);
		if (nread <= 0)
				return 0;

		/* append the string terminator after CR-LF */
		buf[nread+1]='\0';
		while (nread > 0 && (buf[nread-1]=='\r' || buf[nread-1]=='\n')) {
			buf[nread-1]='\0';
			nread--;
		}
		printf("[+] Received string '%s'\n", buf);
		trace( err_msg("(%s) --- received string '%s'",prog_name, buf) );

		/* get the command */
		if (nread > strlen(MSG_GET) && strncmp(buf,MSG_GET,strlen(MSG_GET))==0) {
			/* Get file name */
			char fname[MAX_STR+1];
			strncpy(fname, buf+4, strlen(buf)-4);
			printf("[+] Client asked to send file '%s'\n", fname);
			trace( err_msg("(%s) --- client asked to send file '%s'",prog_name, fname) );

			ret = stat(fname, &info);
			if (ret == 0) {
				FILE *fp;
				if ( (fp=fopen(fname, "rb")) != NULL) {
					//Send +OK CR LR
					Write (connfd, MSG_OK, strlen(MSG_OK) );
					printf("[+] Sent '%s' to client\n", MSG_OK);
					trace( err_msg("(%s) --- sent '%s' to client",prog_name, MSG_OK) );
					//Send B1 B2 B3 B4
					int size = info.st_size;
					uint32_t val = htonl(size);
					Write (connfd, &val, sizeof(size));
					printf("[+] Sent '%d' - converted in network order - to client\n", size);
					trace( err_msg("(%s) --- sent '%d' - converted in network order - to client",prog_name, size) );
					//Send timespan T1 T2 T3 T4
					uint32_t timespan = htonl(info.st_mtime);
					Write (connfd, &timespan, sizeof(timespan));
					printf("[+] Sent '%d' - timespan - to client\n", timespan);
					trace( err_msg("(%s) --- sent '%d' - ctimespan - to client",prog_name, size) );

					/* Read data from file and send it */
	        while(1)
	        {
	            /* First read file in chunks of 1024 bytes */
	            unsigned char buff[CHUNK_SIZE]={0};
	            int nr = fread(buff,sizeof(char),CHUNK_SIZE,fp);
	            printf("[+] -- Bytes read %d \n", nr);

	            /* If read was success, send data. */
	            if(nr > 0)
	            {
	                //printf("[+] -- Sending : %s\n", buff);
	                Write(connfd, buff, sizeof(buff));
	            }

	            /*
	             * There is something tricky going on with read ..
	             * Either there was error, or we reached end of file.
	             */
	            if (nr < CHUNK_SIZE)
	            {
	                if (feof(fp))
	                    printf("[+] -- End of file\n");
	                if (ferror(fp))
	                    printf("[+] -- Error reading\n");
	                break;
	            }
	        }

					printf("[+] Sent file '%s' to client\n", fname);
					trace( err_msg("(%s) --- sent file '%s' to client",prog_name, fname) );
					fclose(fp);
				} else {
					ret = -1;
				}
			}
			if (ret != 0) {
				Write (connfd, MSG_ERR, strlen(MSG_ERR) );
			}
		} else if (nread >= strlen(MSG_QUIT) && strncmp(buf,MSG_QUIT,strlen(MSG_QUIT))==0) {
			printf("[+] --- client asked to terminate connection\n");
			trace( err_msg("(%s) --- client asked to terminate connection", prog_name) );
			ret_val = 1;
			break;
		} else {
			Write (connfd, MSG_ERR, strlen(MSG_ERR) );
		}

	}
	return ret_val;
}


int main (int argc, char *argv[]) {

	int listenfd, connfd, err=0;
	short port;
	struct sockaddr_in servaddr, cliaddr;
	socklen_t cliaddrlen = sizeof(cliaddr);

	/* for errlib to know the program name */
	prog_name = argv[0];

	/* check arguments */
	if (argc!=2)
		err_quit ("usage: %s <port>\n", prog_name);
	port=atoi(argv[1]);

	/* create socket */
	listenfd = Socket(AF_INET, SOCK_STREAM, 0);

	/* specify address to bind to */
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl (INADDR_ANY);

	Bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	printf("[+] socket created\n");
	printf("[+] listening on %s:%u", inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));
	trace ( err_msg("(%s) socket created",prog_name) );
	trace ( err_msg("(%s) listening on %s:%u", prog_name, inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port)) );

	Listen(listenfd, LISTENQ);

	while (1) {
		printf("[+] waiting for connections ...\n");
		trace( err_msg ("(%s) waiting for connections ...", prog_name) );

		connfd = Accept (listenfd, (struct sockaddr *)&cliaddr, &cliaddrlen);
		printf("[+] - new connection from client %s:%u\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
		trace ( err_msg("(%s) - new connection from client %s:%u", prog_name, inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port)) );

		err = receiver(connfd);

		Close (connfd);
		printf("[+] - connection closed\n");
		trace( err_msg ("(%s) - connection closed by %s", prog_name, (err==0)?"client":"server") );
	}
	return 0;
}
