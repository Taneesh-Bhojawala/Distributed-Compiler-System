#ifndef COMMON_H
#define COMMON_H

//All the required libraries
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

#define MAX_BUFF 4096           //buffer size for the network packet
#define PORT 8080               //socket port
#define SERVER_IP "127.0.0.1"   //master ip address

typedef enum
{
    CMD_WORKER_READY,           //sent by worker to indicate readiness
    CMD_SUBMIT_JOB,             //sent by client while sending the c/cpp files
    CMD_RETURN_OBJ,             //sent by workers while sending back the obj files
    CMD_COMPILATION_ERROR,      //sent by workers if there was compilaltion error
    CMD_REGISTER_SESSION,       //sent by client at the very begining to register the session
    CMD_AUTH_SUCCESS,           //sent by master if authenticaion was a success
    CMD_AUTH_FAIL,              //sent by master if authenticaion was a failure
    CMD_RETURN_LOG,             //sent by master to client while sending  the build log
    CMD_FETCH_LOG,              //used by admin and master while transfer of master log
    CMD_SHUTDOWN,               //sent by admin to shutdown the master server instantly
    CMD_ADD_USER                //sent by admin to add user
} CommandType;      //all the different commands that are sent with the network packet to indicate what is happening

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
    char username[32];
    char password[32];
    char role[32];
    char data[MAX_BUFF];
} NetworkPacket;            //the network packet used everytime something is sent using sockets


//common function used by all to connect to the server
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