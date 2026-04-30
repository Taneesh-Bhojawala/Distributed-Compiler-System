#include "logger.h"

//write a message to the global master log with a timestamp
//uses file locking to handle the concurrency
//writes across threads/processes, preventing interleaved log entries
void write_global_log(const char *message)
{
    mkdir("./logs", 0755);
    int fd = open("./logs/master_logs.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    if(fd == -1) return;

    //prepare a file lock structure for an exclusive (write) lock
    //we use F_SETLKW to block until the lock becomes available
    struct flock lck;
    lck.l_type = F_WRLCK;
    lck.l_whence = SEEK_END;
    lck.l_len = 0;
    lck.l_start = 0;
    fcntl(fd, F_SETLKW, &lck);

    time_t curr_time = time(NULL);
    char *dt = ctime(&curr_time);
    dt[strlen(dt)-1] = '\0';

    //make the final log statement
    char log_buff[1024];
    snprintf(log_buff, sizeof(log_buff), "[%s] %s\n", dt, message);
    write(fd, log_buff, strlen(log_buff));

    //release the lock and close the descriptor
    lck.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lck);
    close(fd);
}

//no need for file locking as it is protected by mutex lock for the workers
void write_session_log(const int session_id, const char *message)
{
    mkdir("./logs", 0755);
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "./logs/session_%d.log", session_id);
    
    int fd = open(filepath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd == -1) return;

    char log_buffer[512];
    snprintf(log_buffer, sizeof(log_buffer), "%s\n", message);
    write(fd, log_buffer, strlen(log_buffer));
    
    close(fd);
}