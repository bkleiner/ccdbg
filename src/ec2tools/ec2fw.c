#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "ec2drv.h"


// foward declarations
void print_buf( char *buf, int len );
void progress( uint8_t percent );

EC2DRV ec2obj;

int main(int argc, char *argv[])
{
	int i,in;
	unsigned int csum;
	char ec2fw[8192];

	if(argc!=3)
	{
		printf(	"ec2-update-fw syntax:\n"
				"\tec2-update-fw /dev/ttyS0 ec2-fw-18.bin\n\n" );
		return -1;
	}
	in = open( argv[2], O_RDONLY );
	if( in!=-1 )
	{
		i = read( in, ec2fw, 8192);
		printf("%i bytes read\n",i);
		printf("Updating EC2 Firmware\n");
		ec2_connect( &ec2obj, argv[1] );
		ec2obj.progress_cbk = &progress;
		printf("Firmware update %s\n\n",
		ec2_write_firmware( &ec2obj, ec2fw, i ) ? "PASSED" : "FAILED" );
		close(in);
		ec2_connect( &ec2obj, argv[1] );
		ec2_disconnect( &ec2obj );
	}
	return EXIT_SUCCESS;
}



void progress( uint8_t percent )
{
	printf("%i\n",percent);
}

