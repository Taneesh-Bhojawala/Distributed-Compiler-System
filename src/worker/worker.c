#include "../../include/common.h"
#include "compiler.h"

int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("Use: %s <user_name> <password>\n", argv[0]);
        return -1;
    }

    int sd = connect_to_server("127.0.0.1", PORT);
    if(sd == -1) return -1;
    
    printf("\n+ Worker connected to master\n");

    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = CMD_WORKER_READY;
    strcpy(packet.username, argv[1]);
    strcpy(packet.password, argv[2]);
    strcpy(packet.role, "worker");
    
    send(sd, &packet, sizeof(NetworkPacket), 0);

    NetworkPacket response;
    if(recv(sd, &response, sizeof(NetworkPacket), MSG_WAITALL) <= 0) return -1;

    if(response.type == CMD_AUTH_FAIL)
    {
        printf("Access Denied: %s\n", response.data);
        close(sd);
        return -1;
    }

    printf("\nWorker authenticated. Waiting for jobs...\n");
    mkdir("../../temp", 0777);

    while(1)
    {
        int src_fd = -1;
        char temp_src[256], temp_obj[256], original_filename[256];
        int curr_session = 0;

        snprintf(temp_src, sizeof(temp_src), "../../temp/worker_%d.c", getpid());
        snprintf(temp_obj, sizeof(temp_obj), "../../temp/worker_%d.o", getpid());

        while(1)
        {
            ssize_t bytes = recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            if(bytes <= 0)
            {
                printf("\nMaster disconnected. Shutting down worker.\n");
                close(sd);
                exit(0); 
            }

            if(src_fd == -1)
            {
                curr_session = packet.session_id;
                strcpy(original_filename, packet.file_name); 
                src_fd = open(temp_src, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                printf("\nReceiving code %s from master for session: %d\n", original_filename, curr_session);
            }
            write(src_fd, packet.data, packet.file_size);
            if(packet.is_last_chunk == 1) break;
        }
        if(src_fd != -1) close(src_fd);

        process_compilation(sd, curr_session, original_filename, temp_src, temp_obj);
    }
    return 0;
}