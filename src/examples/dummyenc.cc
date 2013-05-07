#include<iostream>
#include<fstream>
#include<cstdio>
#include<fcntl.h>
#include<unistd.h>

#include<cstdlib>
#include<sys/types.h>
#include<sys/stat.h>
#include<cerrno>

using namespace std;

int main(){
	cout << "Dummy encoder to run with sprout!" << endl;

	// Read the frame information from log file
	// fstream fin;
	// fin.open("frame_size.log", fstream::in);
	int nframes;
	cin >> nframes;
	cout << "Frame count: " << nframes << endl;
	unsigned int *frame_size = new unsigned int[nframes];
	for(int i=0; i<nframes; i++){
		cin >> frame_size[i];
		// cout << "Frame[" << i << "]: " << frame_size[i] << endl;
	}

	// Open a new named pipe if there is none yet
	struct stat pipe_status;
	int stat_err = stat("/tmp/nfifo", &pipe_status);
	if( stat_err < 0 ){
		if( errno == ENOENT ){
			// cout << "Fifo does not exist yet! Trying to create one" << endl;
			int efifo = mkfifo("/tmp/nfifo", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
			if(efifo < 0){
				perror("mkfifo");
				return EXIT_FAILURE;
			}
			cout << "Opened a new fifo at /tmp/nfifo" << endl;
		} else {
			perror("stat");
		}
	}

	// Open a new file descriptor to the named pipe
	int npipe = open("/tmp/nfifo", O_WRONLY);
	if(npipe < 0){
		perror("open");
		return EXIT_FAILURE;
	}

	// Sleep initially to allow sender setup
	sleep(3);

	int ncount = 0;
	while(true){
		unsigned char data[4] = {0};
		for(int i=0; i<4; i++){
			data[i] = (frame_size[ncount] >> (8*i)) & 0xff;
		}

		if(ncount < nframes) {
			int ndata = write(npipe, data, 4);
			if( ndata < 0){
				perror("write");
			} else if ( ndata == 0 ){
				perror("Empty Write!");
			}
		}

		// Sleep to simulate a 60fps frame rate
		// usleep(33333);
		usleep(16666);

		++ncount;
		// cout << "ncount: " << ncount << endl;
		// if(ncount == nframes) break;
	}

	// Clean up!
	// fin.close();
	close(npipe);
	unlink("/tmp/nfifo");

	cout << "Succesful exit for dummy encoder. Bye!" << endl;
	return EXIT_SUCCESS;
}
