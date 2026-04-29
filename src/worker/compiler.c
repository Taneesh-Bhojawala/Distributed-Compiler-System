#include "compiler.h"

void process_compilation(int sd, int curr_session, char *original_filename, char *temp_src, char *temp_obj)
{
    NetworkPacket packet;
    int err_pipe[2];
    
    if(pipe(err_pipe) == -1)
    {
        perror("Error making pipe");
        return;
    }

    char *ext = strrchr(original_filename, '.');
    char *compiler_cmd = "gcc";
    if(ext != NULL && strcmp(ext, ".cpp") == 0) compiler_cmd = "g++";

    int pid = fork();

    if(pid == 0)
    {
        close(err_pipe[0]);
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[1]);
        
        execlp(compiler_cmd, compiler_cmd, "-c", temp_src, "-o", temp_obj, NULL);
        perror("Execlp failed");
        exit(-1);
    } 
    else if(pid > 0)
    {
        close(err_pipe[1]);
        int status;
        waitpid(pid, &status, 0);
        
        if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            printf("Compilation successful. Sending .o file back...\n");
            
            char *dot = strrchr(original_filename, '.'); 
            if(dot != NULL) strcpy(dot, ".o"); 
            else strcat(original_filename, ".o");

            int obj_fd = open(temp_obj, O_RDONLY);
            if(obj_fd == -1) return;
            
            while(1)
            {
                memset(&packet, 0, sizeof(NetworkPacket));
                packet.session_id = curr_session;
                packet.type = CMD_RETURN_OBJ;
                strcpy(packet.role, "worker");
                strcpy(packet.file_name, original_filename);

                packet.file_size = read(obj_fd, packet.data, MAX_BUFF-1);
                packet.is_last_chunk = (packet.file_size < MAX_BUFF-1) ? 1 : 0;

                send(sd, &packet, sizeof(NetworkPacket), 0);
                if(packet.is_last_chunk == 1) break;
            }
            close(obj_fd);
            printf("Object file %s successfully sent back.\n", original_filename);
        }
        else
        {
            printf("Compilation failed for %s\n", original_filename);
            memset(&packet, 0, sizeof(NetworkPacket));
            packet.session_id = curr_session;
            packet.type = CMD_COMPILATION_ERROR;
            strcpy(packet.role, "worker");
            strcpy(packet.file_name, original_filename);

            packet.file_size = read(err_pipe[0], packet.data, MAX_BUFF-1);
            if(packet.file_size > 0) packet.data[packet.file_size] = '\0';
            packet.is_last_chunk = 1;
            
            send(sd, &packet, sizeof(NetworkPacket), 0);
        }
        close(err_pipe[0]);
    }
}