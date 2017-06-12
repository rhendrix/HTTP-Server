#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

#define MYPORT "80"    //the port users will be connecting to
#define BACKLOG 20     //how many pending connections queue will hold
#define NUM_THREADS 10 //number of threads to put in thread pool
#define BUFSIZE 2048   //size of buffer used for read/recv
#define URISIZE 2048   //max uri size/size of buffer for uri 

typedef struct thread_info
{
	pthread_t thread_id; //thread id
	int thread_num;      //thread num (index in threads array)
	int fd;              //fd for use in thread function
	char status;         //0 if thread is available, 1 if in use
} thread_info;

thread_info threads[NUM_THREADS];

//status strings
char* str404 = "HTTP/1.1 404 Not Found\r\n";
char* str200 = "HTTP/1.1 200 Ok\r\n";

//mimetype strings
char* mimeHTML = "text/html\r\n";
char* mimeCSS = "text/css\r\n";
char* mimeJS = "application/javascript\r\n";
char* mimePNG = "image/png\r\n";
char* mimeJPG = "image/jpeg\r\n";
char* mimeICO = "image/x-icon\r\n";
char* mimeMP4 = "video/mp4\r\n";
char* mimeGeneric = "*/*\r\n";

//return mimetype string based on extension of url
char* getMIME(char* URI)
{
	//get pointer to URI extension
	int i;
	for(i=strlen(URI)-1;i>0;i--)
	{
		if(URI[i] == '.') break;
	}
	char* ext = &URI[i+1];

	if(!strcmp(ext, "html")) return mimeHTML;
	else if(!strcmp(ext, "css")) return mimeCSS;
	else if(!strcmp(ext, "js")) return mimeJS;
	else if(!strcmp(ext, "ico")) return mimeICO;
	else if(!strcmp(ext, "png")) return mimePNG;
	else if(!strcmp(ext, "jpg")) return mimeJPG;
	else if(!strcmp(ext, "jpeg")) return mimeJPG;
	else if(!strcmp(ext, "mp4")) return mimeMP4;
	else return mimeGeneric;
}

//get http response header
void getHeader(char* URI, char* statusStr, char* buf)
{
	sprintf(buf, "%sContent-Type: %s\r\n", statusStr, getMIME(URI));
	return;
}

//covert hex values in uri to ascii characters
void decodeURI(char* URI, int start)
{
	int len = strlen(URI);
	char hex[3] = {0};
	int i;
	
	//find the % indicating a hex value
	for(i=start;i<len;i++)
	{
		if(URI[i] == '%')
		{
			hex[0] = URI[i+1];
			hex[1] = URI[i+2];
			break;
		}
	}

	//if there were no percent signs return
	if(!hex[0]) return;
	else
	{
		//convert hex string to char
		char new_char = strtol(hex, NULL, 16);
		
		//change percent sign to corresponding character
		URI[i++] = new_char;

		//move all the characters back two cells to replace the hex value
		for(int j=i;j<len-1;j++)
		{
			URI[j] = URI[j+2];
		}

		//call decodeURI recursively starting after the new char
		decodeURI(URI, i);
		return;
	}
}

void getURI(char* req, char* URI)
{
	//find beginning of URI
	int i = 0;
	while(req[i] != ' ')
	{
		i++;
	}
	i++;

	//copy URI into URI buffer
	int j = 1;
	while(req[i] != ' ')
	{
		URI[j++] = req[i++];
	}
	URI[0] = '.';
	URI[j] = 0;

	//if the uri is "./" change the URI string to "./index.html"
	if(!strcmp(URI, "./"))
	{
		char* indexStr = "./index.html";
		int indexStrLen = strlen(indexStr);
		for(i=0;i<=indexStrLen;i++) URI[i] = indexStr[i];
	}

	//covert ascii codes "%20" to characters
	decodeURI(URI, 0);

	return;
}

void* acceptThread(void* ti)
{
	thread_info* t_info = (thread_info*)ti;
	int new_fd = t_info->fd;
	char* buf = malloc(BUFSIZE);

	printf("Handled by thread %d.\n", t_info->thread_num);

	//receive http request	
	printf("Receiving...\n");
	int written = -1;
	while(written < 0)
	{
		written = recv(new_fd, buf, BUFSIZE, 0);
	}
	buf[written] = 0;
	printf("%s", buf);

	//extract uri from request
	char* URI = malloc(URISIZE);
	getURI(buf, URI);

	//send http response
	printf("Sending...\n");

	int htmlfd, bytes_read, bytes_sent, headerSize;
	char* statusStr;

	//open requested file and check for 404 not found
	htmlfd = open(URI, O_RDONLY);
	if(htmlfd < 0)
	{
		statusStr = str404;
		htmlfd = open("./404.html", O_RDONLY);
	}
	else
	{
		statusStr = str200;
	}
	
	//send header
	memset(buf, 0, BUFSIZE);
	getHeader(URI, statusStr, buf);
	printf("%s", buf);
	headerSize = strlen(buf);

	bytes_sent = 0;
	while(bytes_sent < headerSize)
	{
		bytes_sent += send(new_fd, buf+bytes_sent, headerSize-bytes_sent, 0);
	}

	//send file
	bytes_read = BUFSIZE;
	while(bytes_read == BUFSIZE)
	{
		//clear buffer and read bytes
		memset(buf, 0, BUFSIZE);
		bytes_read = read(htmlfd, buf, BUFSIZE);

		//it's possible for send to not send all of the data
		//so we loop until bytes_sent = bytes_read
		bytes_sent = 0;
		while(bytes_sent < bytes_read)
		{
			bytes_sent += send(new_fd, buf+bytes_sent, bytes_read-bytes_sent, 0);
		}
	}

	printf("Done...\n\n");

	//close files and free memory
	close(htmlfd);
	close(new_fd);
	free(buf);
	free(URI);

	//set thread status
	t_info->status = 0;

	pthread_exit(NULL);
}

int main(void)
{
    struct addrinfo hints, *res;
    int sockfd;

    //load address structs with getaddrinfo()
	int ret = 1;
	while(ret != 0)
	{
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;     //use IPv4 or IPv6
		hints.ai_socktype = SOCK_STREAM; //TCP type socket
		hints.ai_flags = AI_PASSIVE;     //fill in IP automatically

		ret = getaddrinfo(NULL, MYPORT, &hints, &res);
	}

    //make a socket, bind it, and listen on it
	printf("Creating Socket...\n");
	sockfd = -1;
	while(sockfd < 0)
	{
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	}

	//bind socket
	printf("Binding Socket...\n");
	ret = 1;
	while(ret != 0) ret = bind(sockfd, res->ai_addr, res->ai_addrlen);

	//listen on socket
	printf("Listening...\n");
	ret = 1;
    while(ret != 0) ret = listen(sockfd, BACKLOG);

	//create threads for incoming requests
	struct sockaddr_storage their_addr;
	socklen_t addr_size;
	pthread_attr_t pattr;

	//initialize set of threads
	for(int i=0;i<NUM_THREADS;i++)
	{
		threads[i].thread_num = i;
		threads[i].status = 0;
	}

	printf("Accepting...\n");

	while(1)
	{
		for(int i=0;i<NUM_THREADS;i++)
		{
			//check if thread is available
			if(threads[i].status) continue;

			//set thread status
			threads[i].status = 1;

			//set thread attributes object for PTHREAD_CREATE_DETACHED
			memset(&pattr, 0, sizeof(pattr));
			pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);

			//accept the incoming connection:
			addr_size = sizeof their_addr;
			memset(&their_addr, 0, addr_size);
			threads[i].fd = -1;
			while(threads[i].fd < 0)
			{
				threads[i].fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
			}
			pthread_create(&threads[i].thread_id, &pattr, acceptThread, &threads[i]);
			break;
		}
	}

	return 0;
}
