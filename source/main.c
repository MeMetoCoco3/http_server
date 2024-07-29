#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <zlib.h>
#include "header.h"



typedef struct {
  int client_id;
  const char *path;
} clientRequest;

int gzip(const char *input, size_t input_len, char *output, size_t *output_len);
void handle_connection(clientRequest *client);


int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);
	
  int server_fd, client_addr_len, new_socket;
	struct sockaddr_in client_addr;
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
	   								 .sin_port = htons(4221),
	   								 .sin_addr = { htonl(INADDR_ANY) },
	 								};
	
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);
	
  while (1){
    if ((new_socket = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *)&client_addr_len)) < 0){
      perror("Accept error");
    }
  
    pid_t pid = fork();
    if (pid == 0){
      close (server_fd);
      clientRequest *new_client = malloc(sizeof(clientRequest));
      new_client->client_id = new_socket;

      if (argc > 2 && strcmp(argv[1], "--directory") == 0 && strlen(argv[2]) >= 1) {
        new_client->path = argv[2];
      }
      handle_connection(new_client);
      close(new_socket);
      free(new_client);
      exit(0);
    } else if (pid > 0){
      close(new_socket);
    } else {
      perror("Error fork");
    }
  }

  return 0;
}


void handle_connection(clientRequest *client){
  char request[1024];
  char respose[4096];
  char verb[10], path[255], protocol[60], host[50], user_agent[255], encoding[50], body[1024];

  unsigned char* compressed_body[1024];
  size_t output_len = 255;
  
  int n = read(client->client_id, request, sizeof(request));

  if (n < 0){
    perror("Error reading request");
    close(client->client_id);
    exit(1);
  }
  request[n] = '\0'; //Null terminate request.

  char *line = strtok(request, "\r\n");

  if (line == NULL){
    strcpy(respose, msg_404);
  } else {
    sscanf(line, "%s %s %s", verb, path, protocol);
    char *header_line;
    user_agent[0] = '\0';
    while((header_line = strtok(NULL, "\r\n")) != NULL) {
      if (strncmp(header_line, "User-Agent:", 11) == 0){
        strcpy(user_agent, header_line + 12); // Skips User-Agent key and goes to value
      } else if (strncmp(header_line, "Accept-Encoding:", 16) == 0){
        char *ptr = strstr(header_line, " gzip");
        if (ptr){
          strcpy(encoding, "gzip");
        }
      } else {
        strcpy(body, header_line);
      }
    }

    switch (verb[0])
    {
      case 'G':
        // this get line is not neccessary: 
        if (strcmp(verb, "GET") == 0){
          if (strcmp(path, main_path) == 0){
            snprintf(respose, sizeof(respose), "%s\r\n", msg_200);
          } else if (strncmp(path, main_echo_path, 6) == 0){
            if (strcmp(encoding, "gzip") == 0){
              char* body = path + strlen(main_echo_path);
              size_t body_len = strlen(body);

              gzip(body, body_len, (char *) compressed_body, &output_len);

              snprintf(respose, sizeof(respose), "%sContent-Type: text/plain\r\nContent-Encoding: %s\r\nContent-Length: %zu\r\n\r\n",  msg_200, encoding, output_len);
            } else {
              snprintf(respose, sizeof(respose), "%sContent-Type: text/plain\r\nContent-Encoding: %s\r\nContent-Length: %d\r\n\r\n%s",  msg_200, encoding, (int)strlen(path + strlen(main_echo_path)), path + strlen(main_echo_path));
            }
          } else if (strcmp(path, main_user_agent_path) == 0){
            snprintf(respose, sizeof(respose), "%sContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",  msg_200, (int)strlen(user_agent), user_agent);
          } else if (strncmp(path, main_file_path, 7) == 0){
            char *file_name = path + 7;
            char file_path[255], content[255];
            snprintf(file_path, sizeof(file_path), "%s%s", client->path, file_name);

            if (access(file_path, F_OK) != -1){
              FILE *file = fopen(file_path, "r");
              char *content = malloc(sizeof(char)*255);
              fgets(content, 255, file);
              snprintf(respose, sizeof(respose), "%sContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n%s",  msg_200, (int) strlen(content), content);
            } else {
              perror("File not found");
              snprintf(respose, sizeof(respose), "%s\r\n", msg_404);
            }
          } else {
            snprintf(respose, sizeof(respose), "%s\r\n", msg_404);
          }
        }
        break;
      
      case 'P':
        if (strncmp(path, main_file_path, 7) == 0){
          char *file_name = path + 7;
          char file_path[255], content[255];
          snprintf(file_path, sizeof(file_path), "%s%s", client->path, file_name);

          if (access(client->path, F_OK) != -1){
            FILE *file = fopen(file_path, "w");
            fprintf(file, "%s", body);
            snprintf(respose, sizeof(respose), "%sContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n%s",  msg_201, (int) strlen(body), body);
            fclose(file);
          } else {
            perror("Folder not found");
            snprintf(respose, sizeof(respose), "%s\r\n", msg_404);
          }
        } else {
          snprintf(respose, sizeof(respose), "%s\r\n", msg_404);
        }
        break;

      default:
        printf("Default");
    }

    size_t respose_len = strlen(respose);
    size_t bytes_sent = send(client->client_id, respose, respose_len, 0);
    if (bytes_sent == -1) {
        perror("send failed");
    } else if (bytes_sent == 0) {
        printf("No data sent\n");
    } else {
        printf("Sent %zd bytes\n", bytes_sent);
    }

    if (strcmp(encoding, "gzip") == 0) {


      bytes_sent = send(client->client_id, compressed_body, output_len, 0);
      if (bytes_sent == -1) {
          perror("send failed");
      } else if (bytes_sent == 0) {
          printf("No data sent\n");
      } else {
          printf("Sent %zd bytes\n", bytes_sent);
      }
    }

    close(client->client_id);
  }
}

int gzip(const char *input, size_t input_len, char *output, size_t *output_len) {
    z_stream stream;
    int ret;

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    ret = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) return ret;

    stream.next_in = (Bytef *)input;
    stream.avail_in = (uInt)input_len;
    stream.next_out = (Bytef *)output;
    stream.avail_out = (uInt)(*output_len);

    ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        return ret == Z_OK ? Z_BUF_ERROR : ret;
    }

    *output_len = stream.total_out;
    deflateEnd(&stream);
    return Z_OK;
}
