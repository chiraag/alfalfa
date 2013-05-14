#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <assert.h>
#include <list>
#include <sys/time.h>

#include "sproutconn.h"
#include "select.h"

#include "enc-utils.h"

#define MAX_FRAMES 60000 // 1000s of video at 60fps

using namespace std;
using namespace Network;

int main( int argc, char *argv[] )
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	char *ip;
	int port;

	Network::SproutConnection *net;

	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	struct tm *tm=localtime(&tv.tv_sec);
	fprintf(stderr, "Time: %02d:%02d:%02d:%03ld:%03ld = %lu\n", 
			tm->tm_hour, tm->tm_min, tm->tm_sec, (tv.tv_usec/1000), 
			(tv.tv_usec%1000), timestamp());

	if ( argc > 1 ) {
		/* client */

		ip = argv[ 1 ];
		port = atoi( argv[ 2 ] );

		net = new Network::SproutConnection( "4h/Td1v//4jkYhqhLGgegw", ip, port );
	} else {
		fprintf(stderr, "Bad TX config\n");
		return -1;
	}

	fprintf( stderr, "Port bound is %d\n", net->port() );

	Select &sel = Select::get_instance();
	sel.add_fd( net->fd() );
	int e2sd_pipe = -1, e2sc_pipe = -1, s2ec_pipe = -1;
	do{
		close(e2sd_pipe);
		close(e2sc_pipe);
		close(s2ec_pipe);

		e2sd_pipe = open("/tmp/e2sd", O_RDONLY);
		e2sc_pipe = open("/tmp/e2sc", O_RDONLY);
		s2ec_pipe = open("/tmp/s2ec", O_WRONLY);
	} while( (e2sd_pipe < 0) || (e2sc_pipe < 0) || (s2ec_pipe < 0) );
	sel.add_fd(e2sd_pipe);
	sel.add_fd(e2sc_pipe);
	// std::cout << "FIFO available for read" << std::endl;

	const int fallback_interval = 50;

	uint64_t time_of_next_transmission = timestamp() + fallback_interval;
	int frame_size[MAX_FRAMES];
	std::fill_n(frame_size, MAX_FRAMES, -1);
	uint64_t read_time[MAX_FRAMES];
	std::fill_n(read_time, MAX_FRAMES, 0);
	int curr_frame_left = -1;

	bool frame_acked[MAX_FRAMES] = {false};
	int last_lost = -1;
	int last_acked = -1;

	int rd_frame_count = 0;
	int wr_frame_count = 0;

	fprintf( stderr, "Looping...\n" );  

	/* loop */
	while ( 1 ) {
		int bytes_can_send = net->window_size();
		if((curr_frame_left == -1) && (frame_size[rd_frame_count] >= 0)){
			curr_frame_left = frame_size[rd_frame_count];
		}
		int outstanding_bytes = curr_frame_left;
		// for(int i=rd_frame_count+1;i<wr_frame_count;i++){
		// 	outstanding_bytes += frame_size[i];
		// }
		int bytes_to_send = std::max(0, outstanding_bytes);
		int bytes_will_be_sent = std::min( bytes_can_send, bytes_to_send );
		bool sent = false;

		// if(!server){
		// 	fprintf(stderr, "Bytes can send: %d\n", bytes_can_send);
		// 	fprintf(stderr, "FrameID: %d, Bytes Left: %d, Bytes Will be sent: %d\n", 
		// 			rd_frame_count, frame_size[rd_frame_count], bytes_will_be_sent);
		// }

		/* actually send, maybe */
		while( bytes_will_be_sent > 0 ) {
			int this_packet_size = std::min( 1435, bytes_will_be_sent );
			this_packet_size = std::min( curr_frame_left, this_packet_size );
			bytes_will_be_sent -= this_packet_size;
			curr_frame_left -= this_packet_size;
			assert( bytes_will_be_sent >= 0 );
			assert( curr_frame_left >= 0 );
			fprintf(stderr, "SEND [%lu]: FRAME %d [%d/%d]\n", timestamp(), 
					rd_frame_count, frame_size[rd_frame_count]-curr_frame_left, 
					frame_size[rd_frame_count]);

			string garbage( this_packet_size+5, 'x' );
			char frame_count_str[6];
			sprintf(frame_count_str, "%05d", rd_frame_count);
			// fprintf(stderr, "DEBUG: Frame No: %s => %d\n", frame_count_str, rd_frame_count);
			for(int i=0;i<5;i++){
				garbage[i]=frame_count_str[i];
			}

			int time_to_next = 0;
			if ( bytes_will_be_sent == 0 ) {
				time_to_next = fallback_interval;
			}
			net->send( garbage, time_to_next );
			sent = true;

			if(curr_frame_left == 0){
				curr_frame_left = frame_size[rd_frame_count+1];
				fprintf(stderr, "SENT [%lu]: FRAME %d\n", timestamp(), 
						rd_frame_count);
				++rd_frame_count;
			}
		}

		if( (!sent) && (time_of_next_transmission <= timestamp()) ){
			net->send( "", fallback_interval );
			sent = true;
		}

		if(sent){
			time_of_next_transmission = std::max( timestamp() + fallback_interval,
					time_of_next_transmission );
		}

		// fprintf(stderr, "Before: %lu\n", timestamp());
		int active_fds = sel.select( 0 /* wait_time */ );
		if ( active_fds < 0 ) {
			perror( "select" );
			exit( 1 );
		}

		/* receive */
		if ( sel.read( net->fd() ) ) {
			string packet( net->recv() );
			if(packet.length() != 0) {
				Encoder::ACK ack_message;
				ack_message.ParseFromString(packet);

				// fprintf(stderr, "ACK [%lu]: CURR %d [%d/%d]\n", 
				// 		timestamp(), 
				// 		ack_message.curr_frame(), ack_message.curr_bytes(),
				// 		frame_size[ack_message.curr_frame()] );

				if(!frame_acked[ack_message.curr_frame()] && 
						(ack_message.curr_bytes() == frame_size[ack_message.curr_frame()]) ){
					if(ack_message.curr_frame() > last_acked){
						frame_acked[ack_message.curr_frame()] = true;
						last_acked = std::max(last_acked, ack_message.curr_frame());
						fprintf(stderr, "LAT [%lu]: FRAME %d [%lu ms]\n", 
								timestamp(), ack_message.curr_frame(), 
								ack_message.timestamp()-read_time[ack_message.curr_frame()] ); 
					}
				}
			}
		}

		if(sel.read(e2sc_pipe)){
			Encoder::E2SControl control_req = 
				read_message<Encoder::E2SControl>(e2sc_pipe);
			if(control_req.req_forecast()){
				double rate = net->coarse_rate();
				int forecast = rate*33.33;
				int outstanding = curr_frame_left;
				for(int i=rd_frame_count+1;i<wr_frame_count;i++){
					outstanding += frame_size[i];
				}
				bool will_drain = (wr_frame_count <= rd_frame_count+2) || 
					(outstanding < 2*forecast);
				Encoder::S2EControl control_resp;
				control_resp.set_do_encode(will_drain);
				control_resp.set_forecast_byte(forecast);
				while(last_lost < last_acked){
					++last_lost;
					if(!frame_acked[last_lost]){
						control_resp.add_lost_frames(last_lost);
					}
				}
				write_message<Encoder::S2EControl>(s2ec_pipe, control_resp);
				fprintf(stderr, "FBCK [%lu]: RATE %d [%c]\n", 
						timestamp(), forecast, 
						will_drain?'Y':'N' );
			}
		}

		/* setup next frame */
		if(sel.read(e2sd_pipe)){
			Encoder::E2SData frame_data = 
				read_message<Encoder::E2SData>(e2sd_pipe);
			frame_size[wr_frame_count] = frame_data.frame_size();
			read_time[wr_frame_count] = timestamp();
			fprintf(stderr, "READ [%lu]: FRAME %d [%d B]\n", 
					read_time[wr_frame_count], wr_frame_count, 
					frame_size[wr_frame_count] );
			++wr_frame_count;
		}
		// fprintf(stderr, "After: %lu\n", timestamp());
	}
}
