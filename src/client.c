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

    char no_slash_dir[512];
    strcpy(no_slash_dir, dir_path);

    int len = strlen(no_slash_dir);
    if(len>0 && no_slash_dir[len-1] == '/') no_slash_dir[len-1] = '\0';

    char compiled_files_dir[512];
    snprintf(compiled_files_dir, sizeof(compiled_files_dir), "%s_compiled", no_slash_dir);

    mkdir(compiled_files_dir, 0755);
    printf("Compiled files will be save to %s", compiled_files_dir);

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

    int always_on_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in ser_addr;
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    ser_addr.sin_port = htons(PORT);
    if(connect(always_on_socket, (struct sockaddr *) &ser_addr, sizeof(ser_addr)) == -1)
    {
        perror("Always on connection failed");
        return -1;
    }

    NetworkPacket register_packet;
    memset(&register_packet, 0, sizeof(NetworkPacket));

    register_packet.type = CMD_REGISTER_SESSION;
    register_packet.session_id = session_id;
    register_packet.file_size = total_jobs; //used file_size to send the total file count
    strcpy(register_packet.role, "client_control");

    if(send(always_on_socket, &register_packet, sizeof(NetworkPacket), 0) == -1)
    {
        perror("Failed to register session");
        close(always_on_socket);
        return -1;
    }

    printf("Session registered. Master is expecting %d files.\n", total_jobs);

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

    printf("Files uploaded successfully! Waiting for compilation...\n");

    NetworkPacket result;
    while(1)
    {
        ssize_t bytes = recv(always_on_socket, &result, sizeof(NetworkPacket), MSG_WAITALL);

        if(bytes<=0)
        {
            printf("Session closed by master. All jobs finished.\n");
            break;
        }
        if(result.type == CMD_RETURN_OBJ)
        {
            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/%s", compiled_files_dir, result.file_name);
            
            int obj_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(obj_fd == -1)
            {
                perror("Failed to open output file");
                continue;
            }

            write(obj_fd, result.data, result.file_size);
            
            while(result.is_last_chunk == 0)
            {
                recv(always_on_socket, &result, sizeof(NetworkPacket), MSG_WAITALL);
                write(obj_fd, result.data, result.file_size);
            }
            close(obj_fd);
            printf("Saved compiled object to: %s\n", filepath);
        }
        else if(result.type == CMD_COMPILATION_ERROR)
        {
            printf("Error compiling file %s with error:\n%s\n", result.file_name, result.data);
        }
    }
    close(always_on_socket);
    return 0;
}