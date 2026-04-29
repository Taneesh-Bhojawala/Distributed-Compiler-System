#include "../../include/common.h"

int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("Use: %s <admin_username> <password>\n", argv[0]);
        return -1;
    }

    char username[32];
    char password[32];
    strcpy(username, argv[1]);
    strcpy(password, argv[2]);

    printf("   ADMIN FUNCTIONS   \n");

    while(1)
    {
        printf("\n1. Get Master Logs\n");
        printf("2. Add New User\n");
        printf("3. Shutdown Master Server\n");
        printf("4. Exit\n");
        printf("\nSelect an option: ");
        
        int choice;
        scanf("%d", &choice);

        if(choice == 4) break;

        int sd = connect_to_server("127.0.0.1", PORT);
        if(sd == -1)
        {
            printf("Server is offline.\n");
            continue;
        }

        NetworkPacket packet;
        memset(&packet, 0, sizeof(NetworkPacket));
        strcpy(packet.username, username);
        strcpy(packet.password, password);
        strcpy(packet.role, "admin");

        if(choice == 1)
        {
            packet.type = CMD_FETCH_LOG;
            send(sd, &packet, sizeof(NetworkPacket), 0);
            
            int log_fd = -1;
            while(1)
            {
                ssize_t bytes = recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
                if(bytes <= 0) break;

                if(packet.type == CMD_AUTH_FAIL)
                {
                    printf("\nAccess Denied: %s\n", packet.data);
                    break;
                }
                else if(packet.type == CMD_COMPILATION_ERROR)
                {
                    printf("\nServer: %s\n", packet.data);
                    break;
                }
                else if(packet.type == CMD_FETCH_LOG)
                {
                    if(log_fd == -1) log_fd = open("fetched_master_log.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    write(log_fd, packet.data, packet.file_size);
                    if(packet.is_last_chunk == 1)
                    {
                        printf("\nSuccess! Log saved to 'fetched_master_log.log'.\n");
                        break;
                    }
                }
            }
            if(log_fd != -1) close(log_fd);
        }
        else if(choice == 2)
        {
            char n_user[32], n_pass[32], n_role[32];
            printf("Enter new username: ");
            scanf("%s", n_user);
            printf("Enter new password: ");
            scanf("%s", n_pass);
            printf("Enter role (client/worker/admin): ");
            scanf("%s", n_role);

            packet.type = CMD_ADD_USER;
            snprintf(packet.data, sizeof(packet.data), "%s %s %s", n_user, n_pass, n_role);
            send(sd, &packet, sizeof(NetworkPacket), 0);

            recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            if(packet.type == CMD_AUTH_SUCCESS) printf("\nUser '%s' added successfully.\n", n_user);
            else printf("\nFailed: %s\n", packet.data);
        }
        
        else if(choice == 3)
        {
            packet.type = CMD_SHUTDOWN;
            send(sd, &packet, sizeof(NetworkPacket), 0);
            recv(sd, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            
            if(packet.type == CMD_AUTH_SUCCESS) printf("\nServer shut down successfully.\n");
            else printf("\nFailed to shut down.\n");
            
            close(sd);
            break;
        }
        close(sd);
    }
    return 0;
}