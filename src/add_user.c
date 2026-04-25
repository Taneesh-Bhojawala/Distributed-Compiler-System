#include "../include/common.h"

int main(int argc, char *argv[])
{
    if(argc<4)
    {
        printf("Use: %s <user_name> <password> <role>\n", argv[0]);
        printf("Roles: admin, client, worker\n");
        return -1;
    }

    UserDetails new_user;
    memset(&new_user, 0, sizeof(UserDetails));
    strcpy(new_user.username, argv[1]);
    strcpy(new_user.password, argv[2]);
    if(strcmp(argv[3], "admin") != 0 && strcmp(argv[3], "client") != 0 && strcmp(argv[3], "worker") != 0)
    {
        printf("%s is not a valid role\n", argv[3]);
        return -1;
    }
    strcpy(new_user.role, argv[3]);

    int fd = open("users.bin", O_RDWR | O_CREAT | O_APPEND, 0644);
    if(fd == -1)
    {
        perror("Error opening data file");
        return -1;
    }

    struct flock lck;
    lck.l_type = F_WRLCK;
    lck.l_start = 0;
    lck.l_whence = SEEK_SET;
    lck.l_len = 0;

    printf("Checking file\n");
    fcntl(fd, F_SETLKW, &lck);

    UserDetails temp;
    int exists = 0;

    lseek(fd, 0, SEEK_SET);
    while(read(fd, &temp, sizeof(UserDetails)) == sizeof(UserDetails))
    {
        if(temp.username == new_user.username)
        {
            exists = 1;
            break;
        }
    }

    if(exists) printf("User with username: %s already exists, please use another username\n", new_user.username);
    else
    {
        write(fd, &new_user, sizeof(UserDetails));
        printf("User %s successfully registered as %s\n", new_user.username, new_user.role);
    }

    lck.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lck);

    close(fd);
    return 0;
}