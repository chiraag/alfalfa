#include"enc-utils.h"

int create_pipe(const char * pname, int oflags){
	struct stat pipe_status;
	int stat_err = stat(pname, &pipe_status);
	if( stat_err < 0 ){
		if( errno == ENOENT ){
			// cout << "Fifo does not exist yet! Trying to create one" << endl;
			int efifo = mkfifo(pname, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
			if(efifo < 0){
				perror("mkfifo");
				exit(1);
			}
			fprintf(stderr, "Opened a new fifo at %s", pname);
		} else {
			perror("stat");
		}
	}

	// Open a new file descriptor to the named pipe
	int npipe = open(pname, oflags);
	if(npipe < 0){
		perror("open");
		exit(1);
	}

	return npipe;
};
