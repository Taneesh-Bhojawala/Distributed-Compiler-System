#include "auth.h"

//Authenticate a user by checking ./users.bin for matching username/password.
//expected_role: role required for this action (e.g., "client", "worker", "admin").
//Returns:  1 on success (authenticated),
//           0 on failure (not found / wrong credentials),
//          -1 on error opening the users file.
int auth_user(const char *username, const char *password, const char *expected_role)
{
    int fd = open("./users.bin", O_RDONLY);
    if(fd == -1)
    {
        perror("Error opening file, users.bin might not exist");
        return -1;
    }
    //Prepare a read lock to safely read the binary users file(locks entire file)
    struct flock lck;
    lck.l_type = F_RDLCK;
    lck.l_start = 0;
    lck.l_whence = SEEK_SET;
    lck.l_len = 0;
    fcntl(fd, F_SETLKW, &lck);

    UserDetails rec;
    int is_valid = 0;

    //Read each record and compare credentials
    while(read(fd, &rec, sizeof(UserDetails)) == sizeof(UserDetails))
    {
        if(strcmp(username, rec.username) == 0 && strcmp(password, rec.password) == 0)
        {
            //Accept if role matches expected_role OR stored role is "admin"
            if(strcmp(expected_role, rec.role) == 0 || strcmp(rec.role, "admin") == 0)
            {
                is_valid = 1;
                break;
            }
        }
    }
    //release the loack
    lck.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lck);

    close(fd);
    return is_valid;
}

//if users.bin does not exist, ensure 1 admin exists
void master_admin()
{
    int fd = open("users.bin", O_RDONLY);
    if(fd == -1)
    {
        printf("No users.bin found. Setting default admin account (admin/admin)\n");
        fd = open("users.bin", O_WRONLY|O_CREAT, 0644);
        UserDetails default_admin;
        memset(&default_admin, 0, sizeof(UserDetails));
        strcpy(default_admin.username, "admin");
        strcpy(default_admin.password, "admin");
        strcpy(default_admin.role, "admin");

        write(fd, &default_admin, sizeof(UserDetails));
    }
    close(fd);
}