#include <unistd.h>
#include <string>
#include <assert.h>
#include <list>
#include <stdlib.h>

#include "sproutconn.h"
#include "select.h"

using namespace std;
using namespace Network;

enum RC {NONE, MBR, PC, PC_MBR, CR, FORM};

int main( int argc, char *argv[] )
{
	char *ip;
	int port;

	RC rc = FORM;
	double derating = 0.9;
	double MAX_RATE = 4000.0; // In bits per ms
	double rate = 0.0;
	double tau = 10.0; // In ms

	Network::SproutConnection *net;

	bool server = true;

	if ( argc > 1 ) {
		/* client */

		server = false;

		ip = argv[ 1 ];
		port = atoi( argv[ 2 ] );
		if (argc > 3){
			if(string( argv[3] ) == "NONE"){
				rc = NONE;
				fprintf(stderr, "RC: NONE\n");
			} else if (string( argv[3] ) == "MBR"){
				rc = MBR;
				MAX_RATE = strtod(argv[4], NULL);
				fprintf(stderr, "RC: MBR %g\n", MAX_RATE);
			} else if (string( argv[3] ) == "PC"){
				rc = PC;
				derating = strtod(argv[4], NULL);
				fprintf(stderr, "RC: PC %g\n", derating);
			} else if (string( argv[3] ) == "PC_MBR"){
				rc = PC_MBR;
				MAX_RATE = strtod(argv[4], NULL);
				derating = strtod(argv[5], NULL);
				fprintf(stderr, "RC: PC_MBR %g %g\n", MAX_RATE, derating);
			} else if (string( argv[3] ) == "CR"){
				rc = CR;
				fprintf(stderr, "RC: CR\n");
			} else if (string( argv[3] ) == "FORM"){
				rc = FORM;
				fprintf(stderr, "RC: FORM\n");
			} else {
				fprintf(stderr, "Controller Unknown\n");
				exit(1);
			}
		}

		net = new Network::SproutConnection( "4h/Td1v//4jkYhqhLGgegw", ip, port );
	} else {
		net = new Network::SproutConnection( NULL, NULL );
	}

	fprintf( stderr, "Port bound is %d\n", net->port() );

	Select &sel = Select::get_instance();
	sel.add_fd( net->fd() );

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
	uint64_t time_of_last_transmission = timestamp();
	uint64_t time_of_last_update = timestamp();

	fprintf( stderr, "Looping...\n" );  
	fprintf(stderr, "MAX_RATE: %g, Derating: %g\n", MAX_RATE, derating);

	/* loop */
	while ( 1 ) {
		int bytes_to_send = net->window_size();
		if(rc == MBR){
			int bytes_pending = (timestamp()-time_of_last_transmission)*MAX_RATE/8;
			bytes_to_send = std::min(bytes_to_send, bytes_pending);
		} else if (rc == PC){
			bytes_to_send *= derating;
		} else if (rc == PC_MBR){
			int bytes_pending = (timestamp()-time_of_last_transmission)*MAX_RATE/8;
			if (bytes_to_send < bytes_pending){
				bytes_to_send *= derating;
			} else {
				bytes_to_send = bytes_pending;
			}
		} else if (rc == CR){
			rate = net->coarse_rate(); // In bytes per ms
			int bytes_pending = (timestamp()-time_of_last_transmission)*
				std::min(rate, MAX_RATE/8);
			bytes_to_send = std::min(bytes_to_send, bytes_pending);
		} else if (rc == FORM){
			double t = timestamp()-time_of_last_update;
			rate += (1-exp(-1*t/tau))*(bytes_to_send-rate); // In bytes
			int max_rate = (timestamp()-time_of_last_transmission)*MAX_RATE/8;
			int bytes_pending = std::min((int)rate, max_rate);
			// fprintf(stderr, "%g %d %d\n", rate, max_rate, bytes_to_send);
			bytes_to_send = std::min(bytes_to_send, bytes_pending);
			time_of_last_update = timestamp();
		}

		if ( server ) {
			bytes_to_send = 0;
		}

		/* actually send, maybe */
		if ( ( bytes_to_send > 0 ) || ( time_of_next_transmission <= timestamp() ) ) {
			// fprintf(stderr, "rate: %g bytes_pending: %d\n", rate, bytes_pending);
			fprintf(stderr, "bytes_to_send: %d\n", bytes_to_send);
			do {
				int this_packet_size = std::min( 1440, bytes_to_send );
				bytes_to_send -= this_packet_size;
				assert( bytes_to_send >= 0 );

				string garbage( this_packet_size, 'x' );

				int time_to_next = 0;
				if ( bytes_to_send == 0 ) {
					time_to_next = fallback_interval;
				}

				net->send( garbage, time_to_next );
			} while ( bytes_to_send > 0 );

			time_of_last_transmission = timestamp();

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
	}
}
