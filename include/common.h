#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <time.h>

#define MAX_BUFF 65536
#define PORT 8080

typedef enum
{
    CMD_WORKER_READY,
    CMD_SUBMIT_JOB,
    CMD_RETURN_OBJ,
    CMD_COMPILATION_ERROR,
    CMD_REGISTER_SESSION,
    CMD_AUTH_SUCCESS,
    CMD_AUTH_FAIL,
    CMD_RETURN_LOG,
    CMD_FETCH_LOG
} CommandType;

typedef struct
{
    char username[32];
    char password[32];
    char role[32];
} UserDetails;

typedef struct
{
    CommandType type;
    int session_id;
    char file_name[256];
    int file_size;
    int is_last_chunk;
    char data[MAX_BUFF];
    char username[32];
    char password[32];
    char role[32];
} NetworkPacket;

static int connect_to_server(const char *ip, int port)
{
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);
    
    if(connect(sd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
    {
        close(sd);
        return -1;
    }
    return sd;
}

#endif