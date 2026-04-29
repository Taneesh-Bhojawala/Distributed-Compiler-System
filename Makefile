# Compiler settings
CC = gcc
CFLAGS = -I./include -pthread

all: master worker client admin

#Compile the modular Master Server
master:
	$(CC) $(CFLAGS) src/master/*.c -o master

#Compile the Worker files
worker:
	$(CC) $(CFLAGS) src/worker/*.c -o worker

#Compile the Client files
client:
	$(CC) $(CFLAGS) src/client/*.c -o client

#Compile the Admin
admin:
	$(CC) $(CFLAGS) src/admin/*.c -o admin

#Clean up the build files and the log files
clean:
	rm -f master worker client admin
	rm -rf build/* logs/* temp/*