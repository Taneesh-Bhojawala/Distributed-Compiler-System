#include "../include/common.h"


//Session Registry
#define MAX_SESSIONS 10
typedef struct
{
    int session_id;
    int always_on_socket;
    int expected_files;
    int processed_files;
    int is_active;
} SessionInfo;
SessionInfo sessions[MAX_SESSIONS];
pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

//Worker Registry
#define MAX_WORKERS 3
int worker_sockets[MAX_WORKERS];
int worker_busy[MAX_WORKERS];
int workers = 0;
pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t worker_free_cv = PTHREAD_COND_INITIALIZER;

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

    if(listen(server_fd, SOMAXCONN) == -1)
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

        printf("\n+ New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

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
    int soc = *(int*) arg;
    free(arg);

    NetworkPacket packet;
    char dir_path[300];
    char filepath[1024];

    if(recv(soc, &packet, sizeof(NetworkPacket), MSG_WAITALL) <= 0)
    {
        printf("Connection terminated\n");
        close(soc);
        pthread_exit(NULL);
    }

    if(packet.type == CMD_REGISTER_SESSION)
    {
        pthread_mutex_lock(&session_mutex);
        for(int i = 0; i<MAX_SESSIONS; i++)
        {
            if(sessions[i].is_active == 0)
            {
                sessions[i].is_active = 1;
                sessions[i].session_id = packet.session_id;
                sessions[i].always_on_socket = soc;
                sessions[i].expected_files = packet.file_size;  //not file size, just using it to receive total jobs'
                sessions[i].processed_files = 0;
                printf("Session registered: %d with %d expected compiled files\n", packet.session_id, packet.file_size);
                break;
            }
        }
        pthread_mutex_unlock(&session_mutex);
        NetworkPacket temp;
        while(recv(soc, &temp, sizeof(NetworkPacket), 0)){}
        close(soc);
        pthread_exit(NULL);
    }

    else if(packet.type == CMD_WORKER_READY)
    {
        pthread_mutex_lock(&worker_mutex);
        int worker_id = workers;
        worker_sockets[worker_id] = soc;
        worker_busy[worker_id] = 0;
        workers++;

        printf("\nWorker %d connected. Total workers: %d\n", worker_id, workers);
        pthread_mutex_unlock(&worker_mutex);

        int obj_fd = -1;
        while(1)
        {
            ssize_t bytes = recv(soc, &packet, sizeof(NetworkPacket), MSG_WAITALL);
            if(bytes<=0) break;

            if(packet.type == CMD_RETURN_OBJ)
            {
                if(obj_fd == -1)
                {
                    snprintf(dir_path, sizeof(dir_path), "../build/session_%d", packet.session_id);
                    mkdir(dir_path, 0744);
                    snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, packet.file_name);
                    
                    obj_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    printf("\n -> Receiving compiled object: %s from Worker %d\n", packet.file_name, worker_id);
                }
                write(obj_fd, packet.data, packet.file_size);

                pthread_mutex_lock(&session_mutex);
                for(int i = 0; i<MAX_SESSIONS; i++)
                {
                    if(sessions[i].is_active && sessions[i].session_id == packet.session_id)
                    {
                        send(sessions[i].always_on_socket, &packet, sizeof(NetworkPacket), 0);
                        if(packet.is_last_chunk == 1)
                        {
                            sessions[i].processed_files++;
                            if(sessions[i].processed_files == sessions[i].expected_files)
                            {
                                printf("All files compiled and sent to client successfully. Closing socket for session %d\n", sessions[i].session_id);
                                close(sessions[i].always_on_socket);
                                sessions[i].is_active = 0;
                            }
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&session_mutex);

                if(packet.is_last_chunk == 1)
                {
                    printf("Object file saved\n");
                    close(obj_fd);
                    obj_fd = -1;

                    pthread_mutex_lock(&worker_mutex);
                    worker_busy[worker_id] = 0;
                    pthread_cond_signal(&worker_free_cv);
                    pthread_mutex_unlock(&worker_mutex);
                }
            }
            else if(packet.type == CMD_COMPILATION_ERROR)
            {
                pthread_mutex_lock(&session_mutex);
                for(int i = 0; i < MAX_SESSIONS; i++)
                {
                    if(sessions[i].is_active && sessions[i].session_id == packet.session_id)
                    {
                        send(sessions[i].always_on_socket, &packet, sizeof(NetworkPacket), 0);
                        sessions[i].processed_files++;
                        if(sessions[i].processed_files == sessions[i].expected_files)
                        {
                            printf("Session %d complete (with errors).\n", packet.session_id);
                            close(sessions[i].always_on_socket);
                            sessions[i].is_active = 0;
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&session_mutex);

                pthread_mutex_lock(&worker_mutex);
                worker_busy[worker_id] = 0;
                pthread_cond_signal(&worker_free_cv);
                pthread_mutex_unlock(&worker_mutex);
            }
        }
        pthread_mutex_lock(&worker_mutex);
        worker_busy[worker_id] = 0;
        pthread_cond_signal(&worker_free_cv);
        pthread_mutex_unlock(&worker_mutex);
        printf("Worker %d disconnected\n", worker_id);
    }

    else if(packet.type == CMD_SUBMIT_JOB)
    {
        printf("Client submitted file: %s\n", packet.file_name);
        snprintf(dir_path, sizeof(dir_path), "../build/session_%d", packet.session_id);
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
        printf(" -> Source code %s saved. Finding idle worker...\n", packet.file_name);

        int assigned_worker_soc = -1;
        int assigned_worker_id = -1;

        pthread_mutex_lock(&worker_mutex);
        while(assigned_worker_soc == -1)
        {
            for(int i = 0; i<MAX_WORKERS; i++)
            {
                if(worker_busy[i] == 0)
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
            pthread_cond_wait(&worker_free_cv, &worker_mutex);
        }
        pthread_mutex_unlock(&worker_mutex);

        printf("Worker found\n");

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
        printf(" -> Job sent to worker %d.\n", assigned_worker_id);

    }
    close(soc);
    pthread_exit(NULL);
}