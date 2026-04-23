#include "../include/common.h"

void *handle_connection(void *arg);

int main()
{
    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1)
    {
        perror("Socket error");
        exit(-1);
    }

    //just for testing right now, as once program ends, the kernel keeps the port occupied for 2 mins
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if(bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
    {
        perror("Binding failed");
        exit(-1);
    }

    if(listen(server_fd, 10) == -1)
    {
        perror("Listen failed");
        exit(-1);
    }

    printf("Master listening on port %d...\n", PORT);

    while(1)
    {
        client_socket = accept(server_fd, (struct sockaddr *) &client_addr, &addr_len);
        if(client_socket == -1)
        {
            perror("Accept failed");
            continue;
        }

        printf("\n + New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        //need to specifically allocte the memory on the heap or else if global is used, it might be overwritten by the next client before the thread actually schedules
        int *new_socket = malloc(sizeof(int));
        *new_socket = client_socket;

        pthread_t thread_id;
        if(pthread_create(&thread_id, NULL, handle_connection, (void *) new_socket)!=0)
        {
            perror("Failed to create thread");
            free(new_socket);
            close(client_socket);
        }

        pthread_detach(thread_id);

        
    }
    close(server_fd);
    return 0;
}

void *handle_connection(void *arg)
{
    int client_socket = *(int*) arg;
    free(arg);

    NetworkPacket packet;
    char dir_path[300];
    char filepath[1024];
    FILE *fp = NULL;

    while(1)
    {
        ssize_t bytes = recv(client_socket, &packet, sizeof(NetworkPacket), MSG_WAITALL);

        if(bytes<=0) break;

        if(fp == NULL)
        {
            snprintf(dir_path, sizeof(dir_path), "../build/session_%d", packet.session_id);
            mkdir(dir_path, 0744);

            snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, packet.file_name);
            fp = fopen(filepath, "ab");
            printf(" -> Receiving file stream\n");
        }
        fwrite(packet.data, 1, packet.file_size, fp);
        if(packet.is_last_chunk == 1)
        {
            printf("File received completely\n");
            break;
        }
    }
    if(fp != NULL) fclose(fp);
    close(client_socket);

    pthread_exit(NULL);
}