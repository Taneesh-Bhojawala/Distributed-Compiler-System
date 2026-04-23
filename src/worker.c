#include "../include/common.h"
#include <sys/wait.h>

int main()
{
    struct sockaddr_in server_addr;
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if(sd == -1)
    {
        perror("Socket error");
        exit(-1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);

    if(connect(sd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
    {
        perror("Connection Error");
        exit(-1);
    }
    printf("\n+ Worker connected to master\n");

    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = CMD_WORKER_READY;
    strcpy(packet.role, "worker");
    
    if(send(sd, &packet, sizeof(NetworkPacket), 0) == -1)
    {
        perror("Ready signal couldnt be sent");
        close(sd);
        exit(-1);
    }

    printf("\nWorker ready sent. Waiting for job...\n");

    while(1)
    {
        int src_fd = -1;
        char temp_src[256] = "task.cpp";
        char temp_obj[256] = "task.o";
        int curr_session = 0;

        while(1)
        {
            ssize_t bytes = recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            if(src_fd == -1)
            {
                curr_session = packet.session_id;
                src_fd = open(temp_src, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                printf("Receiving code from master for session: %d\n", curr_session);
            }
            write(src_fd, packet.data, packet.file_size);
            if(packet.is_last_chunk == 1) break;
        }
        if(src_fd != -1) close(src_fd);

        int pid = fork();

        if(pid == 0)
        {
            execlp("gcc", "gcc", "-c", temp_src, "-o", temp_obj, NULL);
            perror("Execlp failed");
            exit(-1);
        }
        else
        {
            int status;
            waitpid(pid, &status, 0);
            //from man page
            if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
            {
                printf("Compilation successful. Sending .o file back...\n");
                
                int obj_fd = open(temp_obj, O_RDONLY);
                if(obj_fd == -1)
                {
                    perror("Couldnt open file");
                    continue;
                }
                while (1)
                {
                    memset(&packet, 0, sizeof(NetworkPacket));
                    packet.session_id = curr_session;
                    packet.type = CMD_RETURN_OBJ;
                    strcpy(packet.role, "worker");
                    strcpy(packet.file_name, temp_obj);

                    packet.file_size = read(obj_fd, packet.data, MAX_BUFF-1);

                    if(packet.file_size<MAX_BUFF-1) packet.is_last_chunk = 1;
                    else packet.is_last_chunk = 0;

                    if(send(sd, &packet, sizeof(NetworkPacket), 0) == -1)
                    {
                        perror("Send failed");
                        break;
                    }

                    if(packet.is_last_chunk == 1) break;
                }
                close(obj_fd);
                printf(".o file successfully sent back\n");
            }
            else
            {
                printf("Compilation failed\n");
            }
        }
    }
    return 0;
}