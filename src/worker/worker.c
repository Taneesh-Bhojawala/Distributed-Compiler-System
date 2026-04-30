#include "compiler.h"

//entry point for a worker node process
int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("Use: %s <user_name> <password>\n", argv[0]);
        return -1;
    }

    //connect to master; this socket is used for long-lived worker communication
    int sd = connect_to_server(SERVER_IP, PORT);
    if(sd == -1)
    {
        perror("Could not connect to server");
        return -1;
    }
    
    printf("\n+ Worker connected to master and trying to authenticate\n");

    //prepare and send worker-ready packet with credentials
    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = CMD_WORKER_READY;
    strcpy(packet.username, argv[1]);
    strcpy(packet.password, argv[2]);
    strcpy(packet.role, "worker");
    
    send(sd, &packet, sizeof(NetworkPacket), 0);

    //wait for master response (auth success/fail)
    NetworkPacket response;
    if(recv(sd, &response, sizeof(NetworkPacket), MSG_WAITALL) <= 0) return -1;

    if(response.type == CMD_AUTH_FAIL)
    {
        //authentication rejected by master; exit cleanly
        printf("Access Denied: %s\n", response.data);
        close(sd);
        return -1;
    }

    //ensure temp dir exists for temporary files
    printf("\nWorker authenticated. Waiting for jobs...\n");
    mkdir("./temp", 0777);

    //main loop: receive source file(s) from master and compile them
    while(1)
    {
        int src_fd = -1;
        char temp_src[256], temp_obj[256], original_filename[256];
        int curr_session = 0;

        //receive a file in one or more NetworkPacket chunks
        while(1)
        {
            ssize_t bytes = recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            if(bytes <= 0)
            {
                printf("\nMaster disconnected. Shutting down worker.\n");
                close(sd);
                exit(0); 
            }

            //on first chunk, allocate temp filenames and open temp source file
            if(src_fd == -1)
            {
                curr_session = packet.session_id;
                strcpy(original_filename, packet.file_name); 
                
                //determine extension; default to .c if missing
                char *ext = strrchr(original_filename, '.');
                if(ext == NULL) ext = ".c";
                
                //create unique temporary source/object filenames using process id
                //this prevents races between multiple worker processes on same host
                snprintf(temp_src, sizeof(temp_src), "./temp/worker_%d%s", getpid(), ext);
                snprintf(temp_obj, sizeof(temp_obj), "./temp/worker_%d.o", getpid());

                //open temp source for writing assembled chunks
                src_fd = open(temp_src, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                printf("\nReceiving code %s from master for session: %d\n", original_filename, curr_session);
            }
            //append received chunk to temp source file
            write(src_fd, packet.data, packet.file_size);
            if(packet.is_last_chunk == 1) break;
        }
        if(src_fd != -1) close(src_fd);

        //hand off to compilation routine which forks/execs compiler and streams results back
        process_compilation(sd, curr_session, original_filename, temp_src, temp_obj);
    }
    return 0;
}