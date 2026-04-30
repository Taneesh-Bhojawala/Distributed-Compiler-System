#include "uploader.h"
/**
 * uploader.c
 * Multiple tread safe job queue and uploader
 * maintain a queue of all the filenames to be uploaded
 * handle_upload is the function used by the threads
 */
#define MAX_FILES 1000

char job_queue[MAX_FILES][256];
int total_jobs = 0;             //number of jobs queued
int curr_job_idx = 0;           //index in the job array
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;    //mutex to protect access to the job_queue

//add the filename to the job queue if space is there
int queue_job(const char *filename)
{
    if(total_jobs < MAX_FILES)
    {
        strcpy(job_queue[total_jobs], filename);
        total_jobs++;
        return 0; // Success
    }
    else
    {
        printf("Max file limit reached\n");
        return 1; // Failed (Queue Full)
    }
}

int get_total_jobs()
{
    return total_jobs;
}

//takes the next filename form queue, opens a new connection to the master for each file
//reads the file in chunks and sends it to the master
//mutex protects the job index increment and filename copy. This prevents two threads from processing the same job.
void *handle_upload(void *arg)
{
    PoolArg *args = arg;
    char filename[256];
    char filepath[512];

    while(1)
    {
        pthread_mutex_lock(&queue_mutex);
        if(curr_job_idx >= total_jobs)
        {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        strcpy(filename, job_queue[curr_job_idx]);
        curr_job_idx++;
        pthread_mutex_unlock(&queue_mutex);

        snprintf(filepath, sizeof(filepath), "%s/%s", args->dir_path, filename);

        //open the new connection
        int sd = connect_to_server(SERVER_IP, PORT);
        if(sd == -1)
        {
            perror("Connection failed");
            continue;       //does not retry, goes to next job.
        }

        //open, read and send the file in chunks
        int fd = open(filepath, O_RDONLY);
        if(fd == -1)
        {
            perror("File open error");
            close(sd);
            continue;
        }

        NetworkPacket packet;
        while(1)
        {
            memset(&packet, 0, sizeof(NetworkPacket));
            packet.type = CMD_SUBMIT_JOB; 
            packet.session_id = args->session_id;

            strcpy(packet.username, args->username);
            strcpy(packet.password, args->password);
            strcpy(packet.role, "client");
            strcpy(packet.file_name, filename);

            packet.file_size = read(fd, packet.data, MAX_BUFF - 1);
            
            if(packet.file_size < MAX_BUFF - 1) packet.is_last_chunk = 1;
            else packet.is_last_chunk = 0;

            if(send(sd, &packet, sizeof(NetworkPacket), 0) == -1) break;
            if(packet.is_last_chunk == 1) break;
        }
        printf("Uploaded %s\n", filename);
        close(fd);
        close(sd);
    }
    return NULL;
}