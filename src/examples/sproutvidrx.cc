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
		fprintf(stderr, "Bad RX config\n");
		return -1;
	} else {
		net = new Network::SproutConnection( NULL, NULL );
	}

	fprintf( stderr, "Port bound is %d\n", net->port() );

	Select &sel = Select::get_instance();
	sel.add_fd( net->fd() );

	const int fallback_interval = 20;

	/* wait to get attached */
	while ( 1 ) {
		int active_fds = sel.select( -1 );
		if ( active_fds < 0 ) {
			perror( "select" );
			exit( 1 );
		}

		if ( sel.read( net->fd() ) ) {
			net->recv();
		}

		if ( net->get_has_remote_addr() ) {
			break;
		}
	}

	uint64_t time_of_next_transmission = timestamp() + fallback_interval;
	uint64_t last_rx = 0;
	int frame_size[MAX_FRAMES] = {0};
	int rx_frame_count = 0;

	bool send_ack = false;
	Encoder::ACK ack_message;
	ack_message.set_curr_frame(0);
	ack_message.set_curr_bytes(frame_size[0]);

	fprintf( stderr, "Looping...\n" );  

	/* loop */
	while ( 1 ) {
		bool sent = false;

		/* actually send, maybe */
		if( send_ack || (time_of_next_transmission <= timestamp()) ){
			string ack_message_str;
			ack_message.SerializeToString(&ack_message_str);
			net->send( ack_message_str, fallback_interval );
			if(send_ack) send_ack = false;
			sent = true;
		}

		if(sent){
			time_of_next_transmission = std::max( timestamp() + fallback_interval,
					time_of_next_transmission );
		}


		/* wait */
		int wait_time = time_of_next_transmission - timestamp();
		if ( wait_time < 0 ) {
			wait_time = 0;
		} else if ( wait_time > 10 ) {
			wait_time = 10;
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
			if(packet.length() > 0){
				char frame_count_str[6] = {0};
				for(int i=0;i<5;i++){
					frame_count_str[i] = packet[i];
				}
				int frame_count = stoi(frame_count_str);
				// fprintf(stderr, "DEBUG: Frame No: %s => %d\n", frame_count_str, frame_count);
				frame_size[frame_count] += packet.length()-5;
				ack_message.set_curr_frame(frame_count);
				ack_message.set_curr_bytes(frame_size[frame_count]);
				ack_message.set_timestamp(timestamp());
				send_ack = true;

				while(rx_frame_count < frame_count){
					fprintf(stderr, "Recvd frame %d [%d bytes] recvd at %lu\n", 
							rx_frame_count, frame_size[rx_frame_count], last_rx);
					rx_frame_count++;
				}
				last_rx = timestamp();
			}
		}

		// fprintf(stderr, "After: %lu\n", timestamp());
	}
}
