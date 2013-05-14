#include<iostream>
#include "select.h"
#include"enc-utils.h"

#define TFRAME 33333
// #define TFRAME 16666

using namespace std;

inline uint64_t wait_time(struct timeval begin, struct timeval end, 
		uint64_t max_time){
	uint64_t diff_time = (end.tv_sec*100000 + end.tv_usec)
		- (begin.tv_sec*100000 + begin.tv_usec);
	return max((max_time-diff_time), (uint64_t)0);
};

int main(){
	cout << "Dummy encoder to run with sprout!" << endl;

	GOOGLE_PROTOBUF_VERIFY_VERSION;

	// Read the frame information from log file
	int nframes;
	cin >> nframes;
	cout << "Frame count: " << nframes << endl;
	unsigned int *frame_size = new unsigned int[nframes];
	for(int i=0; i<nframes; i++){
		cin >> frame_size[i];
		// cout << "Frame[" << i << "]: " << frame_size[i] << endl;
	}

	// Return FDs to named-pipes (create not existing)
	int e2sd_pipe = create_pipe("/tmp/e2sd", O_WRONLY);
	int e2sc_pipe = create_pipe("/tmp/e2sc", O_WRONLY);
	int s2ec_pipe = create_pipe("/tmp/s2ec", O_RDONLY);

	Select &sel = Select::get_instance();
	sel.add_fd(s2ec_pipe);

	// Sleep initially to allow sender setup
	sleep(3);

	int ncount = 0;
	bool do_encode = true;
	int forecast = 0;
	while(true){
		struct timeval begin, end;
		gettimeofday(&begin, NULL);
		if(ncount < nframes) {
			Encoder::E2SControl control_req;
			control_req.set_req_forecast(true);
			write_message<Encoder::E2SControl>(e2sc_pipe, control_req);

			int active_fds = sel.select( int(0.5*TFRAME/1000.0) );
			if ( active_fds < 0 ) {
				perror( "select" );
				exit( 1 );
			}

			if(sel.read(s2ec_pipe)){
				Encoder::S2EControl control_resp = 
					read_message<Encoder::S2EControl>(s2ec_pipe);
				do_encode = control_resp.do_encode();
				forecast = control_resp.forecast_byte();
				cout << "Forecast: Frame " << ncount << " - [" << forecast 
					<< "bytes] [" << (do_encode?'Y':'N') << "]" << endl;
				for(int i=0;i<control_resp.lost_frames_size();i++){
					cout << "Lost Frame: " << control_resp.lost_frames(i) << endl;
				}
			}

			if(do_encode){
			// if(true){
				Encoder::E2SData frame_data;
				frame_data.set_frame_size(frame_size[ncount]);
				write_message<Encoder::E2SData>(e2sd_pipe, frame_data);
			}
		}
		gettimeofday(&end, NULL);

		// Sleep to simulate uniform frame rate
		usleep(wait_time(begin, end, TFRAME));
		++ncount;
	}

	// Clean up!
	close(e2sd_pipe);	unlink("/tmp/e2sd");
	close(e2sc_pipe);	unlink("/tmp/e2sc");
	close(s2ec_pipe);	unlink("/tmp/s2ec");

	cout << "Succesful exit for dummy encoder. Bye!" << endl;
	return EXIT_SUCCESS;
}
