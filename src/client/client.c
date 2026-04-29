#include "uploader.h"

#define THREAD_POOL 20

int main(int argc, char *argv[])
{
    if(argc!=4)
    {
        printf("Use: %s <directory_name> <user_name> <password>\n", argv[0]);
        return -1;
    }

    char *dir_path = argv[1];
    char *username = argv[2];
    char *password = argv[3];
    int session_id = (int)getpid();

    char no_slash_dir[512];
    strcpy(no_slash_dir, dir_path);

    int len = strlen(no_slash_dir);
    if(len>0 && no_slash_dir[len-1] == '/') no_slash_dir[len-1] = '\0';

    DIR *dir = opendir(dir_path);
    if(dir == NULL)
    {
        perror("Error opening directory\n");
        return -1;
    }

    char compiled_files_dir[512];
    snprintf(compiled_files_dir, sizeof(compiled_files_dir), "%s_compiled", no_slash_dir);

    mkdir(compiled_files_dir, 0755);

    printf("DISTRIBUTED COMPILER CLIENT\n");
    printf("Session ID: %d\n", session_id);
    printf("Starting scan to upload files from directory: %s...\n", dir_path);
    printf("Compiled files will be save to %s\n", compiled_files_dir);

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(strstr(entry->d_name, ".c") || strstr(entry->d_name, ".cpp"))
        {
            if(queue_job(entry->d_name) != 0)
            {
                break;
            }
        }
    }
    closedir(dir);

    int final_total_jobs = get_total_jobs();

    if(final_total_jobs == 0)
    {
        printf("No source file of from .c or .cpp found\n");
        return -1;
    }

    int always_on_socket = connect_to_server("127.0.0.1", PORT);
    if(always_on_socket == -1)
    {
        perror("Connection to client alwasy on failed");
        return -1;
    }

    NetworkPacket register_packet;
    memset(&register_packet, 0, sizeof(NetworkPacket));

    register_packet.type = CMD_REGISTER_SESSION;
    register_packet.session_id = session_id;
    register_packet.file_size = final_total_jobs; //used file_size to send the total file count
    strcpy(register_packet.username, username);
    strcpy(register_packet.password, password);
    strcpy(register_packet.role, "client");

    if(send(always_on_socket, &register_packet, sizeof(NetworkPacket), 0) == -1)
    {
        perror("Failed to register session");
        close(always_on_socket);
        return -1;
    }

    NetworkPacket response;
    if(recv(always_on_socket, &response, sizeof(NetworkPacket), MSG_WAITALL) <= 0)
    {
        printf("Connection dropped by Master during authentication.\n");
        close(always_on_socket);
        return -1;
    }

    if (response.type == CMD_AUTH_FAIL)
    {
        printf("Access Denied: %s\n", response.data);
        close(always_on_socket);
        return -1;
    }

    printf("Session registered. Master is expecting %d files.\n", final_total_jobs);

    pthread_t threads[THREAD_POOL];
    PoolArg arg;
    strcpy(arg.dir_path, dir_path);
    arg.session_id = session_id;
    strcpy(arg.username, username);
    strcpy(arg.password, password);

    int req_threads;
    if(final_total_jobs < THREAD_POOL) req_threads = final_total_jobs;
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
        else if(result.type == CMD_RETURN_LOG)
        {
            char log_filepath[1024];
            snprintf(log_filepath, sizeof(log_filepath), "%s/%s", compiled_files_dir, "build_log.log");
            
            int log_fd = open(log_filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(log_fd != -1)
            {
                write(log_fd, result.data, result.file_size);

                while(result.is_last_chunk == 0)
                {
                    recv(always_on_socket, &result, sizeof(NetworkPacket), MSG_WAITALL);
                    write(log_fd, result.data, result.file_size);
                }              
                close(log_fd);
                printf("\nSession build report successfully saved to: %s\n", log_filepath);
            }
        }
    }
    close(always_on_socket);
    return 0;
}