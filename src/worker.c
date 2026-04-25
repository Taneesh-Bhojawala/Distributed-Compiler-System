#include "../include/common.h"


int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("Use: %s <user_name> <password>\n", argv[0]);
        return -1;
    }

    int sd = connect_to_server("127.0.0.1", PORT);
    if(sd == -1)
    {
        perror("Failed to connect to master");
        return -1;
    }
    printf("\n+ Worker connected to master\n");

    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = CMD_WORKER_READY;
    strcpy(packet.username, argv[1]);
    strcpy(packet.password, argv[2]);
    strcpy(packet.role, "worker");
    
    if(send(sd, &packet, sizeof(NetworkPacket), 0) == -1)
    {
        perror("Ready signal couldnt be sent");
        close(sd);
        exit(-1);
    }

    NetworkPacket response;
    if(recv(sd, &response, sizeof(NetworkPacket), MSG_WAITALL) <= 0)
    {
        printf("Connection dropped by Master during authentication.\n");
        close(sd);
        return -1;
    }

    if (response.type == CMD_AUTH_FAIL)
    {
        printf("Access Denied: %s\n", response.data);
        close(sd);
        return -1;
    }

    printf("\nWorker authenticated. Waiting for job...\n");

    mkdir("../temp", 0777);

    while(1)
    {
        int src_fd = -1;
        char temp_src[256];
        char temp_obj[256];
        int curr_session = 0;
        char original_filename[256];

        snprintf(temp_src, sizeof(temp_src), "../temp/worker_%d.c", getpid());
        snprintf(temp_obj, sizeof(temp_obj), "../temp/worker_%d.o", getpid());

        while(1)
        {
            ssize_t bytes = recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            
            if (bytes <= 0) {
                printf("\nMaster disconnected. Shutting down worker.\n");
                close(sd);
                exit(0); 
            }

            if(src_fd == -1)
            {
                curr_session = packet.session_id;
                
                strcpy(original_filename, packet.file_name); 
                
                src_fd = open(temp_src, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (src_fd == -1)
                {
                    perror("Worker failed to create temp source file");
                    break;
                }
                printf("\nReceiving code %s from master for session: %d\n", original_filename, curr_session);
            }
            write(src_fd, packet.data, packet.file_size);
            if(packet.is_last_chunk == 1) break;
        }
        if(src_fd != -1) close(src_fd);

        int err_pipe[2];
        if(pipe(err_pipe) == -1)
        {
            perror("Error making pipe");
            continue;
        }

        int pid = fork();

        if(pid == 0)
        {
            close(err_pipe[0]);
            dup2(err_pipe[1], 2);
            close(err_pipe[1]);
            
            execlp("gcc", "gcc", "-c", temp_src, "-o", temp_obj, NULL);
            perror("Execlp failed");
            exit(-1);
        }
        else if (pid > 0)
        {
            close(err_pipe[1]);
            int status;
            waitpid(pid, &status, 0);
            
            if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
            {
                printf("Compilation successful. Sending .o file back...\n");
                
                //convert "file1.c" into "file1.o"
                char *dot = strrchr(original_filename, '.'); 
                if(dot != NULL) strcpy(dot, ".o"); 
                else strcat(original_filename, ".o");

                int obj_fd = open(temp_obj, O_RDONLY);
                if(obj_fd == -1)
                {
                    perror("Couldnt open compiled object file");
                    continue;
                }
                
                while (1)
                {
                    memset(&packet, 0, sizeof(NetworkPacket));
                    packet.session_id = curr_session;
                    packet.type = CMD_RETURN_OBJ;
                    strcpy(packet.role, "worker");
                    
                    strcpy(packet.file_name, original_filename);

                    packet.file_size = read(obj_fd, packet.data, MAX_BUFF-1);

                    if(packet.file_size < MAX_BUFF-1) packet.is_last_chunk = 1;
                    else packet.is_last_chunk = 0;

                    if(send(sd, &packet, sizeof(NetworkPacket), 0) == -1)
                    {
                        perror("Send failed");
                        break;
                    }

                    if(packet.is_last_chunk == 1) break;
                }
                close(obj_fd);
                printf("Object file %s successfully sent back.\n", original_filename);

            }
            else
            {
                printf("Compilation failed for %s\n", original_filename);

                memset(&packet, 0, sizeof(NetworkPacket));
                packet.session_id = curr_session;
                packet.type = CMD_COMPILATION_ERROR;
                strcpy(packet.role, "worker");
                strcpy(packet.file_name, original_filename);

                packet.file_size = read(err_pipe[0], packet.data, MAX_BUFF-1);

                if(packet.file_size>0) packet.data[packet.file_size] = '\0';

                packet.is_last_chunk = 1;
                send(sd, &packet, sizeof(NetworkPacket), 0);
            }
            close(err_pipe[0]);
        }
    }
    return 0;
}