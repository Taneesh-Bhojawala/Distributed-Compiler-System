#include "../include/common.h"

int main(int argc, char *argv[])
{
    if(argc<2)
    {
        printf("Use: %s <file_to_send>\n", argv[0]);
        return -1;
    }

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
    NetworkPacket packet;
    
    if(connect(sd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
    {
        perror("Connection failed. Master may be down");
        exit(-1);
    }

    //making custom packet for testing
    memset(&packet, 0, sizeof(NetworkPacket));  //required as when testing found that there may be garbage value, like if file size if only 50B, rest will be filled with garbage in the data array
    packet.session_id = 996;
    strcpy(packet.role, "admin");
    strcpy(packet.file_name, argv[1]);

    FILE *fp = fopen(argv[1], "r");
    if(fp == NULL)
    {
        perror("Error");
        close(sd);
        return -1;
    }

    fseek(fp, 0, SEEK_SET);
    packet.file_size = fread(packet.data, 1, MAX_BUFF-1, fp);
    fclose(fp);

    if(write(sd, &packet, sizeof(NetworkPacket)) == -1)
    {
        perror("Write failed");
        return -1;
    }
    
    printf(" + File uploaded!\n");
    close(sd);
    return 0;
}