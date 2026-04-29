#include "logger.h"
#include "auth.h"
#include "controller.h"

int main()
{
    master_admin(); // Creates users.bin if it doesn't exist
    init();  // Sets all worker sockets to -1

    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1)
    {
        perror("Socket error");
        exit(-1);
    }

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

    if(listen(server_fd, SOMAXCONN) == -1)
    {
        perror("Listen failed");
        exit(-1);
    }

    printf("Master listening on port %d...\n", PORT);
    write_global_log("Master server booted and listening for connections.");

    while(1)
    {
        client_socket = accept(server_fd, (struct sockaddr *) &client_addr, &addr_len);
        if(client_socket == -1)
        {
            perror("Accept failed");
            continue;
        }

        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "New socket connection from %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        write_global_log(log_msg);
        printf("\n+ %s\n", log_msg);

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