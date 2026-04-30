#include "compiler.h"

//handles compiling a single source file and returning results to master
void process_compilation(int sd, int curr_session, char *original_filename, char *temp_src, char *temp_obj)
{
    
    NetworkPacket packet;
    int err_pipe[2];
    
    //create a pipe to capture compiler stderr output
    //parent will read from err_pipe[0], child writes to err_pipe[1]
    if(pipe(err_pipe) == -1)
    {
        perror("Error making pipe");
        return;
    }

    //pick compiler based on file extension; default to gcc
    char *ext = strrchr(original_filename, '.');
    char *compiler_cmd = "gcc";
    if(ext != NULL && strcmp(ext, ".cpp") == 0) compiler_cmd = "g++";

    int pid = fork();

    if(pid == 0)
    {
        //child: execute compiler, route stderr into err_pipe[1]
        close(err_pipe[0]);
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[1]);
        
        //execlp replaces child process with compiler program
        execlp(compiler_cmd, compiler_cmd, "-c", temp_src, "-o", temp_obj, NULL);
        perror("Execlp failed");
        exit(-1);
    } 
    else if(pid > 0)
    {
        //parent: close write end and wait for child to finish
        close(err_pipe[1]);
        int status;
        waitpid(pid, &status, 0);
        
        //if compilation succeeded (exit code 0), read the produced .o and send it
        if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            printf("Compilation successful. Sending .o file back...\n");
            
            //name the filename to have the .o extension now
            char *dot = strrchr(original_filename, '.'); 
            if(dot != NULL) strcpy(dot, ".o"); 
            else strcat(original_filename, ".o");

            //open compiled object and stream in chunks using NetworkPacket
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
        //compilation failed: stream stderr from the child back to master as error chunks
        else
        {
            printf("Compilation failed for %s\n", original_filename);
            
            while(1)
            {
                memset(&packet, 0, sizeof(NetworkPacket));
                packet.session_id = curr_session;
                packet.type = CMD_COMPILATION_ERROR;
                strcpy(packet.role, "worker");
                strcpy(packet.file_name, original_filename);

                //read compiler stderr from pipe
                packet.file_size = read(err_pipe[0], packet.data, MAX_BUFF-1);
                
                if(packet.file_size < MAX_BUFF - 1) packet.is_last_chunk = 1;
                else packet.is_last_chunk = 0;
                
                if(packet.is_last_chunk == 1 && packet.file_size >= 0 && packet.file_size < MAX_BUFF) 
                {
                    packet.data[packet.file_size] = '\0';
                }
                
                send(sd, &packet, sizeof(NetworkPacket), 0);
                if(packet.is_last_chunk == 1) break;
            }
        }
        close(err_pipe[0]);
    }
}