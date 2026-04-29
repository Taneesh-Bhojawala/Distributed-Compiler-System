#ifndef UPLOADER_H
#define UPLOADER_H

#include "../../include/common.h"

typedef struct
{
    char dir_path[256];
    int session_id;
    char username[32];
    char password[32];
} PoolArg;

int queue_job(const char *filename);
int get_total_jobs();
void *handle_upload(void *arg);

#endif