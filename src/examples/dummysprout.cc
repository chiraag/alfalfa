#include<iostream>
#include<cstdio>
#include<fcntl.h>
#include<unistd.h>

#include<cstdlib>
#include<sys/types.h>
#include<sys/stat.h>
#include<cerrno>
#include"../util/select.h"

using namespace std;

int main(){
	cout << "Unit Test for select() and mkfifo()" << endl;

	Select &sel = Select::get_instance();

	int npipe = -1;
	do{
		npipe = open("/tmp/nfifo", O_RDONLY);
	} while( npipe < 0 );
	sel.add_fd(npipe);
	cout << "FIFO available for read" << endl;

	int ncount = 0;
	while(true){
		unsigned char data[4];

		int active_fds = sel.select(-1);
		if( active_fds < 0 ){
			perror("select");
			return EXIT_FAILURE;
		}

		if(sel.read(npipe)){
			++ncount;
			int ndata = read(npipe, data, 4);
			if( ndata < 0){
				perror("read");
			} else if ( ndata > 0 ){
				unsigned int frame_size = (data[3] << 24) + (data[2] << 14) + (data[1] << 8) + data[0]; 
				cout << "Frame[" << ncount << "]: " << frame_size << endl;
			} else {
				break;
			}
		}
	}
			
	// cout << "Count is " << ncount << endl;
	close(npipe);

	cout << "Successful exit for reader. Bye!" << endl;
	return EXIT_SUCCESS;
}
