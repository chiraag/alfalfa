#include<iostream>
#include<cstdio>
#include<cstdlib>

#include<fcntl.h>
#include<unistd.h>

#include<sys/types.h>
#include<sys/stat.h>
#include<cerrno>

#include"swrite.h"
#include"sread.h"
#include"encodermessage.pb.h"

#include <time.h>
#include <sys/time.h>

int create_pipe(const char * pname, int oflags);

template <class Message> void write_message(int npipe, Message data){
	int data_size = data.ByteSize();
	char *data_str = new char[data_size];
	data.SerializeToArray(data_str, data_size);
	write_message_to_pipe(npipe, data_str, data_size);
	delete[] data_str;
};


template <class Message> Message read_message(int npipe){
	ssize_t frame_data_len = read_length_from_pipe(npipe);
	char *frame_data_str = new char[frame_data_len];
	read_message_from_pipe(npipe, frame_data_str, frame_data_len);
	Message frame_data;
	frame_data.ParseFromArray(frame_data_str, frame_data_len);
	delete[] frame_data_str;
	return frame_data;
}
