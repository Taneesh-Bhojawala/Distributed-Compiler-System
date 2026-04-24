#include "../include/common.h"

#define THREAD_POOL 20
#define MAX_FILES 1000

//Queue for the different jobs
char job_queue[MAX_FILES][256];
int total_jobs = 0;
int curr_job_idx = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    char dir_path[512];
    int session_id;
} PoolArg;

void *handle_upload(void *arg)
{
    PoolArg *args = arg;
    char filename[256];
    char filepath[1024];

    while(1)
    {
        pthread_mutex_lock(&queue_mutex);
        if(curr_job_idx>=total_jobs)
        {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        strcpy(filename, job_queue[curr_job_idx]);
        curr_job_idx++;
        pthread_mutex_unlock(&queue_mutex);

        snprintf(filepath, sizeof(filepath), "%s/%s", args->dir_path, filename);

        struct sockaddr_in server_addr;
        int sd = socket(AF_INET, SOCK_STREAM, 0);
        if(sd == -1) continue;

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(PORT);
        
        if(connect(sd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
        {
            perror("Connection failed");
            close(sd);
            continue;
        }

        int fd = open(filepath, O_RDONLY);
        if(fd == -1)
        {
            perror("File open error");
            close(sd);
            continue;
        }

        NetworkPacket packet;
        while(1)
        {
            memset(&packet, 0, sizeof(NetworkPacket));
            packet.type = CMD_SUBMIT_JOB; 
            packet.session_id = args->session_id;
            strcpy(packet.role, "client");
            strcpy(packet.file_name, filename);

            packet.file_size = read(fd, packet.data, MAX_BUFF - 1);
            
            if(packet.file_size < MAX_BUFF - 1) packet.is_last_chunk = 1;
            else packet.is_last_chunk = 0;

            if(send(sd, &packet, sizeof(NetworkPacket), 0) == -1) break;
            if(packet.is_last_chunk == 1) break;
        }
        printf("Uploaded %s\n", filename);
        close(fd);
        close(sd);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if(argc<2)
    {
        printf("Use: %s <directory_name>\n", argv[0]);
        return -1;
    }

    char *dir_path = argv[1];
    int session_id = (int)getpid();

    DIR *dir = opendir(dir_path);
    if(dir == NULL)
    {
        perror("Error opening directory\n");
        return -1;
    }

    printf("DISTRIBUTED COMPILER CLIENT\n");
    printf("Session ID: %d\n", session_id);
    printf("Starting scan to upload files from directory: %s...\n", dir_path);

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(strstr(entry->d_name, ".c") || strstr(entry->d_name, ".cpp"))
        {
            if(total_jobs<MAX_FILES)
            {
                strcpy(job_queue[total_jobs], entry->d_name);
                total_jobs++;
            }
            else
            {
                printf("Max file limit reached\n");
                break;
            }
        }
    }
    closedir(dir);

    if(total_jobs == 0)
    {
        printf("No source file of from .c or .cpp found\n");
        return -1;
    }

    printf("Found %d source files. Starting send to server...\n", total_jobs);

    pthread_t threads[THREAD_POOL];
    PoolArg arg;
    strcpy(arg.dir_path, dir_path);
    arg.session_id = session_id;

    int req_threads;
    if(total_jobs<THREAD_POOL) req_threads = total_jobs;
    else req_threads = THREAD_POOL;

    for(int i = 0; i<req_threads; i++)
    {
        if(pthread_create(&threads[i], NULL, handle_upload, &arg) !=0)
        {
            perror("Failed to create thread");
        }
    }

    for(int i = 0; i<req_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("Files uploaded successfully!\n");
}