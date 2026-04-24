#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

#define MAX_BUFF 65536
#define PORT 8080

typedef enum
{
    CMD_WORKER_READY,
    CMD_SUBMIT_JOB,
    CMD_RETURN_OBJ
} CommandType;

typedef struct
{
    CommandType type;
    int session_id;
    char file_name[256];
    int file_size;
    int is_last_chunk;
    char role[16];
    char data[MAX_BUFF];
} NetworkPacket;

#endif