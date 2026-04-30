#include "controller.h"
#include "logger.h"
#include "auth.h"

//Session Registry
#define MAX_SESSIONS 10
typedef struct
{
    int session_id;
    int always_on_socket;
    int expected_files;
    int processed_files;
    int error_count;
    int is_active;
    pthread_mutex_t socket_mutex;
} SessionInfo;
SessionInfo sessions[MAX_SESSIONS];
pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

//Worker Registry
#define MAX_WORKERS 20
int worker_sockets[MAX_WORKERS];
int worker_busy[MAX_WORKERS];
int workers = 0;
pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t worker_free_cv = PTHREAD_COND_INITIALIZER;

void init()
{
    for(int i = 0; i<MAX_WORKERS; i++) worker_sockets[i] = -1;
}

void reject(int soc, NetworkPacket *packet, const char *reject_msg, const char *log_msg)
{
    packet->type = CMD_AUTH_FAIL;
    strcpy(packet->data, reject_msg);
    send(soc, packet, sizeof(NetworkPacket), 0);
    write_global_log(log_msg);
    printf("%s\n", log_msg);
    close(soc);
    pthread_exit(NULL);
}

void close_session(int s_idx) 
{
    char log_buf[512];

    if(sessions[s_idx].error_count == 0)
    {
        snprintf(log_buf, sizeof(log_buf), "SESSION COMPLETE: Session %d finished compiling all %d files successfully.", sessions[s_idx].session_id, sessions[s_idx].expected_files);
        write_global_log(log_buf);
        write_session_log(sessions[s_idx].session_id, "\n--- All Jobs Completed Successfully ---");
        printf("%s\n", log_buf); 
    }
    else
    {
        snprintf(log_buf, sizeof(log_buf), "SESSION COMPLETE: Session %d finished with %d ERRORS.", sessions[s_idx].session_id, sessions[s_idx].error_count);
        write_global_log(log_buf);
        write_session_log(sessions[s_idx].session_id, "\n--- Session Finished (Some Errors Encountered) ---");
        printf("%s\n", log_buf); 
    }

    char session_log_path[512];
    snprintf(session_log_path, sizeof(session_log_path), "./logs/session_%d.log", sessions[s_idx].session_id);
    
    int log_fd = open(session_log_path, O_RDONLY);
    if (log_fd != -1) 
    {
        while(1)
        {
            NetworkPacket log_packet;
            memset(&log_packet, 0, sizeof(NetworkPacket));
            log_packet.type = CMD_RETURN_LOG;
            
            log_packet.file_size = read(log_fd, log_packet.data, MAX_BUFF - 1);
            
            if(log_packet.file_size < MAX_BUFF - 1) log_packet.is_last_chunk = 1;
            else log_packet.is_last_chunk = 0;
            
            send(sessions[s_idx].always_on_socket, &log_packet, sizeof(NetworkPacket), 0);
            
            if(log_packet.is_last_chunk == 1) break;
        }
        close(log_fd);
    }
    
    shutdown(sessions[s_idx].always_on_socket, SHUT_RDWR);
    close(sessions[s_idx].always_on_socket);
    sessions[s_idx].is_active = 0;
}

void *handle_connection(void *arg)
{
    int soc = *(int*) arg;
    free(arg);

    NetworkPacket packet;
    char dir_path[300];
    char filepath[1024];
    char log_buf[512];

    if(recv(soc, &packet, sizeof(NetworkPacket), MSG_WAITALL) <= 0)
    {
        write_global_log("A socket connection was terminated unexpectedly.");
        printf("A socket connection was terminated unexpectedly.\n");
        close(soc);
        pthread_exit(NULL);
    }

    if(!auth_user(packet.username, packet.password, packet.role))
    {
        snprintf(log_buf, sizeof(log_buf), "AUTH REJECTED: Invalid credentials for user '%s' (Role: %s)", packet.username, packet.role);
        reject(soc, &packet, "Invalid username or password", log_buf);
    }

    if((packet.type == CMD_REGISTER_SESSION || packet.type == CMD_SUBMIT_JOB) && (strcmp(packet.role, "client") != 0))
    {
        snprintf(log_buf, sizeof(log_buf), "DENIED: User '%s' lacks client permissions.", packet.username);
        reject(soc, &packet, "Do not have permission to submit job", log_buf);
    }
    else if((packet.type == CMD_WORKER_READY) && (strcmp(packet.role, "worker") != 0))
    {
        snprintf(log_buf, sizeof(log_buf), "DENIED: User '%s' lacks worker permissions.", packet.username);
        reject(soc, &packet, "Do not have 'worker' permission", log_buf);
    }

    if(packet.type == CMD_REGISTER_SESSION)
    {
        int empty_found = -1;
        pthread_mutex_lock(&session_mutex);
        for(int i = 0; i<MAX_SESSIONS; i++)
        {
            if(sessions[i].is_active == 0)
            {
                sessions[i].is_active = 1;
                sessions[i].session_id = packet.session_id;
                sessions[i].always_on_socket = soc;
                sessions[i].expected_files = packet.file_size;  
                sessions[i].processed_files = 0;
                sessions[i].error_count = 0;
                pthread_mutex_init(&sessions[i].socket_mutex, NULL);
                
                snprintf(log_buf, sizeof(log_buf), "SESSION STARTED: ID %d expecting %d files.", packet.session_id, packet.file_size);
                write_global_log(log_buf);
                write_session_log(packet.session_id, "--- Session Registered Successfully ---");
                printf("%s\n", log_buf);
                empty_found = i;
                break;
            }
        }
        pthread_mutex_unlock(&session_mutex);

        if(empty_found == -1)
        {
            snprintf(log_buf, sizeof(log_buf), "REJECTED: Client '%s' dropped. Max sessions (%d) reached.", packet.username, MAX_SESSIONS);
            reject(soc, &packet, "Server is at maximum capacity. Try again later.", log_buf);
        }
        NetworkPacket success;
        memset(&success, 0, sizeof(NetworkPacket));
        success.type = CMD_AUTH_SUCCESS;
        send(soc, &success, sizeof(NetworkPacket), 0);

        NetworkPacket temp;
        while(recv(soc, &temp, sizeof(NetworkPacket), 0)){}
        close(soc);
        pthread_exit(NULL);
    }

    else if(packet.type == CMD_WORKER_READY)
    {
        pthread_mutex_lock(&worker_mutex);
        int worker_id = -1;
        for(int i = 0; i<MAX_WORKERS; i++)
        {
            if(worker_sockets[i] == -1)
            {
                worker_id = i;
                worker_sockets[worker_id] = soc;
                worker_busy[worker_id] = 0;
                workers++;
                break;
            }
        }
        pthread_mutex_unlock(&worker_mutex);
        
        if(worker_id == -1)
        {
            snprintf(log_buf, sizeof(log_buf), "REJECTED: Worker '%s' dropped. Max capacity (%d) reached.", packet.username, MAX_WORKERS);
            reject(soc, &packet, "Cluster is at maximum capacity.", log_buf);
        }

        NetworkPacket success;
        memset(&success, 0, sizeof(NetworkPacket));
        success.type = CMD_AUTH_SUCCESS;
        send(soc, &success, sizeof(NetworkPacket), 0);

        snprintf(log_buf, sizeof(log_buf), "WORKER JOINED: Node assigned ID %d. Total cluster size: %d", worker_id, workers);
        write_global_log(log_buf);
        printf("%s\n", log_buf);
        
        int obj_fd = -1;
        int error_locked = 0;
        while(1)
        {
            ssize_t bytes = recv(soc, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            if(bytes<=0) break;

            if(packet.type == CMD_RETURN_OBJ)
            {
                int s_idx = -1;
                
                pthread_mutex_lock(&session_mutex);
                for(int i = 0; i < MAX_SESSIONS; i++) 
                {
                    if(sessions[i].is_active && sessions[i].session_id == packet.session_id) 
                    {
                        s_idx = i;
                        break;
                    }
                }
                pthread_mutex_unlock(&session_mutex);

                if (s_idx != -1) 
                {
                    if(obj_fd == -1)
                    {
                        pthread_mutex_lock(&sessions[s_idx].socket_mutex);
                        
                        snprintf(dir_path, sizeof(dir_path), "./build/session_%d", packet.session_id);
                        mkdir(dir_path, 0744);
                        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, packet.file_name);
                        
                        obj_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        
                        snprintf(log_buf, sizeof(log_buf), "[RECV] Object file %s returned by Worker %d for Session %d", packet.file_name, worker_id, packet.session_id);
                        write_global_log(log_buf);
                        printf("%s\n", log_buf);
                    }
                    
                    write(obj_fd, packet.data, packet.file_size);

                    send(sessions[s_idx].always_on_socket, &packet, sizeof(NetworkPacket), 0);

                    if(packet.is_last_chunk == 1)
                    {
                        close(obj_fd);
                        obj_fd = -1;

                        pthread_mutex_lock(&session_mutex);
                        sessions[s_idx].processed_files++;
                        
                        snprintf(log_buf, sizeof(log_buf), "[SUCCESS] Saved %s. Progress: %d/%d", packet.file_name, sessions[s_idx].processed_files, sessions[s_idx].expected_files);
                        write_session_log(packet.session_id, log_buf);
                        printf("%s\n", log_buf);
                                
                        if(sessions[s_idx].processed_files == sessions[s_idx].expected_files) close_session(s_idx);
                        pthread_mutex_unlock(&session_mutex);

                        pthread_mutex_unlock(&sessions[s_idx].socket_mutex);

                        pthread_mutex_lock(&worker_mutex);
                        worker_busy[worker_id] = 0;
                        pthread_cond_signal(&worker_free_cv);
                        pthread_mutex_unlock(&worker_mutex);
                    }
                }
            }
            else if(packet.type == CMD_COMPILATION_ERROR)
            {
                int s_idx = -1;
                
                pthread_mutex_lock(&session_mutex);
                for(int i = 0; i < MAX_SESSIONS; i++) 
                {
                    if(sessions[i].is_active && sessions[i].session_id == packet.session_id) 
                    {
                        s_idx = i;
                        break;
                    }
                }
                pthread_mutex_unlock(&session_mutex);

                if (s_idx != -1) 
                {
                    if(error_locked == 0)
                    {
                        pthread_mutex_lock(&sessions[s_idx].socket_mutex);
                        error_locked = 1;
                    }
                    
                    send(sessions[s_idx].always_on_socket, &packet, sizeof(NetworkPacket), 0);

                    if(packet.is_last_chunk == 1)
                    {
                        pthread_mutex_lock(&session_mutex);
                        sessions[s_idx].processed_files++;
                        sessions[s_idx].error_count++;
                        
                        snprintf(log_buf, sizeof(log_buf), "[ERROR] Failed to compile %s. Progress: %d/%d", packet.file_name, sessions[s_idx].processed_files, sessions[s_idx].expected_files);
                        write_session_log(packet.session_id, log_buf);
                        printf("%s\n", log_buf);
                                    
                        if(sessions[s_idx].processed_files == sessions[s_idx].expected_files) close_session(s_idx);
                        pthread_mutex_unlock(&session_mutex);

                        error_locked = 0;
                        pthread_mutex_unlock(&sessions[s_idx].socket_mutex);

                        pthread_mutex_lock(&worker_mutex);
                        worker_busy[worker_id] = 0;
                        pthread_cond_signal(&worker_free_cv);
                        pthread_mutex_unlock(&worker_mutex);
                    }
                }
            }
        }
        pthread_mutex_lock(&worker_mutex);
        worker_sockets[worker_id] = -1;
        worker_busy[worker_id] = 0;
        workers--;
        pthread_mutex_unlock(&worker_mutex);
        
        snprintf(log_buf, sizeof(log_buf), "WORKER DISCONNECTED: Node ID %d left the cluster.", worker_id);
        write_global_log(log_buf);
        printf("%s\n", log_buf);
    }

    else if(packet.type == CMD_SUBMIT_JOB)
    {
        snprintf(log_buf, sizeof(log_buf), "[UPLOAD] Receiving source file: %s", packet.file_name);
        write_session_log(packet.session_id, log_buf);
        write_global_log(log_buf);
        printf("%s\n", log_buf);
        
        snprintf(dir_path, sizeof(dir_path), "./build/session_%d", packet.session_id);
        mkdir(dir_path, 0744);
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, packet.file_name);
        
        int src_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        write(src_fd, packet.data, packet.file_size);
        while(packet.is_last_chunk == 0)
        {
            recv(soc, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            write(src_fd, packet.data, packet.file_size);
        }
        close(src_fd);

        int assigned_worker_soc = -1;
        int assigned_worker_id = -1;

        pthread_mutex_lock(&worker_mutex);
        while(assigned_worker_soc == -1)
        {
            for(int i = 0; i<MAX_WORKERS; i++)
            {
                if(worker_sockets[i] != -1 && worker_busy[i] == 0)
                {
                    worker_busy[i] = 1;
                    assigned_worker_soc = worker_sockets[i];
                    assigned_worker_id = i;
                    break;
                }
            }
            if(assigned_worker_soc != -1)
            {
                break;
            }

            snprintf(log_buf, sizeof(log_buf), "[QUEUE] All workers busy. Waiting to dispatch: %s", packet.file_name);
            write_global_log(log_buf);
            printf("\033[1;33m%s\033[0m\n", log_buf);

            pthread_cond_wait(&worker_free_cv, &worker_mutex);
        }
        pthread_mutex_unlock(&worker_mutex);

        int session_id = packet.session_id;
        char file_name[256];
        strcpy(file_name, packet.file_name);

        int file_fd = open(filepath, O_RDONLY);
        while(1)
        {
            memset(&packet, 0, sizeof(NetworkPacket));
            packet.session_id = session_id; 
            packet.type = CMD_SUBMIT_JOB;
            strcpy(packet.role, "master");
            strcpy(packet.file_name, file_name); 

            packet.file_size = read(file_fd, packet.data, MAX_BUFF - 1);
            if(packet.file_size < MAX_BUFF - 1) packet.is_last_chunk = 1;
            else packet.is_last_chunk = 0;

            send(assigned_worker_soc, &packet, sizeof(NetworkPacket), 0);
            if(packet.is_last_chunk == 1) break;
        }
        close(file_fd);
        
        snprintf(log_buf, sizeof(log_buf), "[DISPATCH] Routed Session %d file '%s' to Worker %d.", session_id, file_name, assigned_worker_id);
        write_global_log(log_buf);
        printf("%s\n", log_buf);
    }

    else if(packet.type == CMD_FETCH_LOG)
    {
        if(strcmp(packet.role, "admin") != 0)
        {            
            snprintf(log_buf, sizeof(log_buf), "SECURITY: Unauthorized admin access attempt by user '%s'", packet.username);
            reject(soc, &packet, "Access Denied: Admin permissions required", log_buf);
        }

        char admin_name[256];
        strcpy(admin_name, packet.username);

        int log_fd = open("./logs/master_logs.log", O_RDONLY);
        if(log_fd == -1)
        {
            packet.type = CMD_COMPILATION_ERROR; 
            strcpy(packet.data, "Master log is empty or missing on server.");
            send(soc, &packet, sizeof(NetworkPacket), 0);
        }
        else
        {
            while(1)
            {
                memset(&packet, 0, sizeof(NetworkPacket));
                packet.type = CMD_FETCH_LOG;
                strcpy(packet.file_name, "master_logs.log");

                packet.file_size = read(log_fd, packet.data, MAX_BUFF - 1);
                if(packet.file_size < MAX_BUFF - 1) packet.is_last_chunk = 1;
                else packet.is_last_chunk = 0;

                send(soc, &packet, sizeof(NetworkPacket), 0);
                if(packet.is_last_chunk == 1) break;
            }
            close(log_fd);
            
            snprintf(log_buf, sizeof(log_buf), "ADMIN: Global audit log downloaded by admin '%s'", admin_name);
            write_global_log(log_buf);
            printf("%s\n", log_buf);
        }
    }

    else if(packet.type == CMD_SHUTDOWN)
    {
        if(strcmp(packet.role, "admin") != 0)
        {
            snprintf(log_buf, sizeof(log_buf), "SECURITY: Invalid user %s with role %s tried to shutdown Master.", packet.username, packet.role);
            reject(soc, &packet, "Access Denied: Admin permissions required.", log_buf);
        }

        write_global_log("CRITICAL: Master server shutdown triggered by Admin.");
        printf("\n[!] Shutdown command received from Admin. Terminating server...\n");

        packet.type = CMD_AUTH_SUCCESS;
        send(soc, &packet, sizeof(NetworkPacket), 0);
        close(soc);
        
        exit(0);
    }

    else if(packet.type == CMD_ADD_USER)
    {
        if(strcmp(packet.role, "admin") != 0)
        {
            snprintf(log_buf, sizeof(log_buf), "SECURITY: Invalid user %s with role %s tried to add user.", packet.username, packet.role);
            reject(soc, &packet, "Access Denied: Admin privileges required.", log_buf);
        }

        UserDetails new_user;
        memset(&new_user, 0, sizeof(UserDetails));
        sscanf(packet.data, "%s %s %s", new_user.username, new_user.password, new_user.role);
        int fd = open("users.bin", O_WRONLY|O_APPEND, 0644);

        struct flock lck;
        lck.l_type = F_WRLCK;
        lck.l_whence = SEEK_END;
        lck.l_len = 0;
        lck.l_start = 0;
        fcntl(fd, F_SETLKW, &lck);
        write(fd, &new_user, sizeof(UserDetails));

        lck.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lck);

        snprintf(log_buf, sizeof(log_buf), "ADMIN: Added new %s account '%s'.", new_user.role, new_user.username);
        write_global_log(log_buf);
        printf("%s\n", log_buf);
        
        packet.type = CMD_AUTH_SUCCESS;
        send(soc, &packet, sizeof(NetworkPacket), 0);
    }

    close(soc);
    pthread_exit(NULL);
}