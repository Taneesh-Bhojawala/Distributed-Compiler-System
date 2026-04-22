#include "../include/common.h"

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
    }
}