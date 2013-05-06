#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <assert.h>
#include <list>

#include "sproutconn.h"
#include "select.h"

#define MAX_FRAMES 60000 // 1000s of video at 60fps

using namespace std;
using namespace Network;

int main( int argc, char *argv[] )
{
	char *ip;
	int port;

	Network::SproutConnection *net;

	bool server = true;

	if ( argc > 1 ) {
		/* client */

		server = false;

		ip = argv[ 1 ];
		port = atoi( argv[ 2 ] );

		net = new Network::SproutConnection( "4h/Td1v//4jkYhqhLGgegw", ip, port );
	} else {
		net = new Network::SproutConnection( NULL, NULL );
	}

	fprintf( stderr, "Port bound is %d\n", net->port() );

	Select &sel = Select::get_instance();
	sel.add_fd( net->fd() );
	int npipe = -1;
	if(!server){
		do{
			npipe = open("/tmp/nfifo", O_RDONLY);
		} while( npipe < 0 );
		sel.add_fd(npipe);
		// cout << "FIFO available for read" << endl;
	}

	const int fallback_interval = server ? 20 : 50;

	/* wait to get attached */
	if ( server ) {
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
	}

	uint64_t time_of_next_transmission = timestamp() + fallback_interval;
	int frame_size[MAX_FRAMES] = {-1};
	int rd_frame_count = 0;
	int wr_frame_count = 0;

	fprintf( stderr, "Looping...\n" );  

	/* loop */
	while ( 1 ) {
		int bytes_can_send = net->window_size();
		if ( server ) {
			bytes_can_send = 0;
		}
		int bytes_to_send = std::max(0, frame_size[rd_frame_count]);
		int bytes_will_be_sent = std::min( bytes_can_send, bytes_to_send );
		bool sent = false;

		if(!server){
			fprintf(stderr, "Bytes can send: %d\n", bytes_can_send);
			fprintf(stderr, "FrameID: %d, Bytes Left: %d, Bytes Will be sent: %d\n", 
					rd_frame_count, frame_size[rd_frame_count], bytes_will_be_sent);
		}

		/* actually send, maybe */
		while( bytes_will_be_sent > 0 ) {
			int this_packet_size = std::min( 1440, bytes_will_be_sent );
			bytes_will_be_sent -= this_packet_size;
			frame_size[rd_frame_count] -= this_packet_size;
			bytes_to_send -= this_packet_size;
			bytes_can_send -= this_packet_size;
			assert( bytes_will_be_sent >= 0 );

			string garbage( this_packet_size, 'x' );
			int time_to_next = 0;
			if ( bytes_will_be_sent == 0 ) {
				time_to_next = fallback_interval;
			}
			net->send( garbage, time_to_next );
			sent = true;

			if(frame_size[rd_frame_count] == 0){
				++rd_frame_count;
				fprintf(stderr, "Done sending frame %d at %lu\n", rd_frame_count-1, 
						timestamp());
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


		/* wait */
		int wait_time = time_of_next_transmission - timestamp();
		if ( wait_time < 0 ) {
			wait_time = 0;
		} else if ( wait_time > 10 ) {
			wait_time = 10;
		}

		int active_fds = sel.select( wait_time );
		if ( active_fds < 0 ) {
			perror( "select" );
			exit( 1 );
		}

		/* receive */
		if ( sel.read( net->fd() ) ) {
			string packet( net->recv() );
		}

		/* setup next frame */
		if(!server){
			if(sel.read(npipe)){
				unsigned char data[4];
				int ndata = read(npipe, data, 4);
				if( ndata < 0){
					perror("read");
				} else if ( ndata > 0 ){
					unsigned int nxt_frame_size = 
						(data[3] << 24) + (data[2] << 14) + (data[1] << 8) + data[0]; 
					frame_size[wr_frame_count] = nxt_frame_size;
					++wr_frame_count;
					fprintf(stderr, "Done reading in frame %d at %lu\n", wr_frame_count-1, 
							timestamp());
					// cout << "Frame[" << frame_count << "]: " << frame_size << endl;
				} 
			}
		}
	}
}
