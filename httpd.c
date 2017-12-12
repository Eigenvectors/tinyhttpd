/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"		//CRLF

void *accept_request(void *tclient);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void *accept_request(void *tclient)		//new thread must execute this kind function
{
 	int client = *(int *)tclient;	//force type convert
 	char buf[1024];
 	int numchars;

 	char method[255];
 	char url[255];
 	char path[512];

 	size_t i, j;
 	struct stat st;
 	int cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
 	char *query_string = NULL;

 	numchars = get_line(client, buf, sizeof(buf));	//the data's format is process by browser
 	i = 0; j = 0;
 	while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
 	{
  		method[i] = buf[j];	//the array is used to store http method
  		i++; j++;
 	}
 	method[i] = '\0';	//get method character from request line

 	if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))	//only implement two method
 	{
  		unimplemented(client);
  		return NULL;
 	}

 	if (strcasecmp(method, "POST") == 0)	//this is a CGI program
  		cgi = 1;	//first place	POST method need cgi program

 	i = 0;
 	while (ISspace(buf[j]) && (j < sizeof(buf)))	//skip all blank characters
  		j++;
 	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
 	{
  		url[i] = buf[j];
  		i++; j++;
 	}
 	url[i] = '\0';	//get url from request line

 	if (strcasecmp(method, "GET") == 0)
 	{
  		query_string = url;
  		while ((*query_string != '?') && (*query_string != '\0'))
   			query_string++;
  		if (*query_string == '?')
  		{
   			cgi = 1;	//second place		sometime GET method need cgi program
   			*query_string = '\0';
   			query_string++;	//parameter section and anchor section
  		}
 	}

 	sprintf(path, "htdocs%s", url);		//the file name is store into path
 	if (path[strlen(path) - 1] == '/')	//if user don't supply file name, use the default file name
  		strcat(path, "index.html");
 	if (stat(path, &st) == -1) 	//if get the status of file is failed, read & discard request headers
	{							
  		while ((numchars > 0) && strcmp("\n", buf))  /* read & discard request headers */
   			numchars = get_line(client, buf, sizeof(buf));
  		not_found(client);
 	}
 	else
 	{
  		if ((st.st_mode & S_IFMT) == S_IFDIR)	//path is a directory 	It's impossible?????????????
   			strcat(path, "/index.html");
  		if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))	//anyone can execute the file
   			cgi = 1;	//third place		html file can't execute
  		if (!cgi)	//if the program is not a cgi program, send the regular file to client
   			serve_file(client, path);	/* if the request is not POST, don't supply parameter and the file can't
										   be executed, the server will send the html file to client directly */
  		else	//cgi program
   			execute_cgi(client, path, method, query_string);
 	}

 	close(client);
 	return NULL;	//I add it by myself??????????????
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
 	char buf[1024];

 	sprintf(buf, "HTTP/1.0 400 Bad Request\r\n");	//send back status line
 	send(client, buf, sizeof(buf), 0);
 	sprintf(buf, "Content-type: text/html\r\n");	//send back message headers
 	send(client, buf, sizeof(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, sizeof(buf), 0);
 	sprintf(buf, "<P>Your browser sent a bad request, ");	//send back response text
 	send(client, buf, sizeof(buf), 0);
 	sprintf(buf, "such as a POST without a Content-Length.\r\n");
 	send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 	char buf[1024];

 	fgets(buf, sizeof(buf), resource);
 	while (!feof(resource))
 	{
  		send(client, buf, strlen(buf), 0);	//don't send '\0'
  		fgets(buf, sizeof(buf), resource);
 	}
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
 	char buf[1024];

 	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "Content-type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates last system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 	perror(sc);
 	exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method, const char *query_string)
{
 	char buf[1024];
 	int cgi_output[2];
 	int cgi_input[2];
 	pid_t pid;
 	int status;
 	int i;
 	char c;
 	int numchars = 1;
 	int content_length = -1;

 	buf[0] = 'A'; buf[1] = '\0';
 	if (strcasecmp(method, "GET") == 0)
  		while ((numchars > 0) && strcmp("\n", buf))  /* read & discard request headers */
   			numchars = get_line(client, buf, sizeof(buf));
 	else    /* POST */
 	{
  		numchars = get_line(client, buf, sizeof(buf));	//read request headers
  		while ((numchars > 0) && strcmp("\n", buf))		//only get the Content-Length from request headers
  		{
   			buf[15] = '\0';
   			if (strcasecmp(buf, "Content-Length:") == 0)
    			content_length = atoi(&(buf[16]));	//get content length
   			numchars = get_line(client, buf, sizeof(buf));
  		}
  		if (content_length == -1) 
		{
   			bad_request(client);
   			return;
  		}
 	}

 	sprintf(buf, "HTTP/1.0 200 OK\r\n");	//send back status line
 	send(client, buf, strlen(buf), 0);

 	if (pipe(cgi_output) < 0) 
	{
  		cannot_execute(client);
  		return;
 	}
 	if (pipe(cgi_input) < 0) 
	{
  		cannot_execute(client);
  		return;
 	}

 	if ( (pid = fork()) < 0 ) 
	{
  		cannot_execute(client);
  		return;
 	}
 	if (pid == 0)  /* child process: execute CGI script */
 	{
  		char meth_env[255];
  		char query_env[255];
  		char length_env[255];

  		dup2(cgi_output[1], 1);	//redirect to stdout
  		dup2(cgi_input[0], 0);	//redirect to stdin
  		close(cgi_output[0]);
  		close(cgi_input[1]);
  		sprintf(meth_env, "REQUEST_METHOD=%s", method);
  		putenv(meth_env);
  		if (strcasecmp(method, "GET") == 0) 
		{
   			sprintf(query_env, "QUERY_STRING=%s", query_string);	//include parameter and anchor
   			putenv(query_env);
  		}
  		else 
		{   /* POST */
   			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   			putenv(length_env);
  		}
  		execl(path, path, NULL);	//execute cgi script
  		exit(0);
 	} 
	else 
	{    /* parent process send the result of execution to client */
  		close(cgi_output[1]);
  		close(cgi_input[0]);
  		if (strcasecmp(method, "POST") == 0)
   			for (i = 0; i < content_length; i++) 
			{
    			recv(client, &c, 1, 0);		//read request data from client
    			write(cgi_input[1], &c, 1);
   			}
  		while (read(cgi_output[0], &c, 1) > 0)		//there are some problems????????????????
   			send(client, &c, 1, 0);

  		close(cgi_output[0]);
  		close(cgi_input[1]);
  		waitpid(pid, &status, 0);
 	}
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline(\n),
 * carriage return(\r), or a CRLF combination(\r\n).  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)	//all string be sotre in buf end of '\n' and '\0'
{
 	int i = 0;
 	char c = '\0';	//'\0' is null character
 	int n;

 	while ((i < size - 1) && (c != '\n'))	//remain a store unit for '\0'
 	{
  		n = recv(sock, &c, 1, 0);	//read 1 byte once
 		/* DEBUG printf("%02X\n", c); */
  		if (n > 0)
  		{
   			if (c == '\r')
   			{
    			n = recv(sock, &c, 1, MSG_PEEK);
   				/* DEBUG printf("%02X\n", c); */
    			if ((n > 0) && (c == '\n'))	
     				recv(sock, &c, 1, 0);
    			else
     				c = '\n';
   			}
   			buf[i] = c;
   			i++;
  		}
  		else
   			c = '\n';
 	}
 	buf[i] = '\0';
 
 	return(i);	//the length not include '\0'
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
 	char buf[1024];
 	(void)filename;  /* could use filename to determine file type */

 	strcpy(buf, "HTTP/1.0 200 OK\r\n");	//send back the status line
 	send(client, buf, strlen(buf), 0);
 	strcpy(buf, SERVER_STRING);			//send back the message headers
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	strcpy(buf, "\r\n");				//send back blank line
 	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
 	char buf[1024];

 	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, SERVER_STRING);
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "your request because the resource specified\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "is unavailable or nonexistent.\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "</BODY></HTML>\r\n");
 	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
 	FILE *resource = NULL;		//file pointer
 	int numchars = 1;
 	char buf[1024];

 	buf[0] = 'A'; buf[1] = '\0';
 	while ((numchars > 0) && strcmp("\n", buf))  /* read & discard request headers */
  		numchars = get_line(client, buf, sizeof(buf));

 	resource = fopen(filename, "r");	//open a file on only read mode
 	if (resource == NULL)
  		not_found(client);
 	else
 	{
  		headers(client, filename);
  		cat(client, resource);
 	}
 	fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
 	int httpd = 0;
 	struct sockaddr_in server_name;	//server address struct

 	httpd = socket(PF_INET, SOCK_STREAM, 0);	//use TCP 
 	if (httpd == -1)
  		error_die("socket");
 	memset(&server_name, 0, sizeof(server_name));
 	server_name.sin_family = AF_INET;
 	server_name.sin_port = htons(*port);
 	server_name.sin_addr.s_addr = htonl(INADDR_ANY);	//use random local ip address
 	if (bind(httpd, (struct sockaddr *)&server_name, sizeof(server_name)) < 0) 
  		error_die("bind");		//bind socket with address struct
 	if (*port == 0)  /* if dynamically allocating a port */
 	{
  		socklen_t namelen = sizeof(server_name);
  		if (getsockname(httpd, (struct sockaddr *)&server_name, &namelen) == -1)	//get the actual port number
   			error_die("getsockname");
  		*port = ntohs(server_name.sin_port);
 	}
 	if (listen(httpd, 5) < 0)	//maximum 5 connection request wait to accept in queue
  		error_die("listen");
 	return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)	//only implement two method
{
 	char buf[1024];

 	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");	//store the format string into buf
 	send(client, buf, strlen(buf), 0);	//send the content in buf to client
 	sprintf(buf, SERVER_STRING);
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "</TITLE></HEAD>\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "</BODY></HTML>\r\n");
 	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
	int server_sock = -1;	//server socket file descriptor
 	u_short port = 0;		//maybe just a integer type
 	int client_sock = -1;
 	struct sockaddr_in client_name;		//address struct
 	socklen_t client_name_len = sizeof(client_name);	//the length of address struct
 	pthread_t newthread;	

 	server_sock = startup(&port);
 	printf("httpd running on port %d\n", port);		//this port is dynamical allocate

 	while (1)
 	{
		/* client_name be used to store client address struct, this function return client sockfd */
  		client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
  		if (client_sock == -1)
   			error_die("accept");
 		/* accept_request(client_sock); */
 		if (pthread_create(&newthread , NULL, accept_request, (void *)&client_sock) != 0)
   			perror("pthread_create");	//strerror()
 	}

 	close(server_sock);

 	return(0);
}
