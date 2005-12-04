/** ec2writeflash utility
  *
  *   Copyright (C) 2005 by Ricky White
  *   rickyw@neatstuff.co.nz
  *
  *   This program is free software; you can redistribute it and/or modify
  *   it under the terms of the GNU General Public License as published by
  *   the Free Software Foundation; either version 2 of the License, or
  *   (at your option) any later version.
  *
  *   This program is distributed in the hope that it will be useful,
  *   but WITHOUT ANY WARRANTY; without even the implied warranty of
  *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *   GNU General Public License for more details.
  *
  *   You should have received a copy of the GNU General Public License
  *   along with this program; if not, write to the
  *   Free Software Foundation, Inc.,
  *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include "ec2drv.h"
#include "ihex.h"

void help()
{
	printf("ec2writeflash\n"
		   "syntax:\n"
		   "\tec2writeflash --port=/dev/ttyS0 --start=0x0000 --bin file.bin\n"
		   "\twhere /dev/ttyS0 is your desired serial port"
		   "and file.bin is the file to write to flash\n"
		   "\n"
		   "Options:\n"
		   "\t--hex                 File to upload is an intel hex format file\n"
		   "\t--bin                 File to upload is a binary format file\n"
		   "\t--port <serial dev>   Specify serial port to connect to EC2 on\n"
		   "\t--start <addr>        Address to write binary file too ( --bin mode only)\n"
		   "\t--eraseall            Force complete erase of the devices flash memory\n"
		   "\t--scratch             Cause write to occure in scratchpad area of flash\n"
		   "\t--help                Display this help\n"
		   "\n");
}

EC2DRV ec2obj;
#define MAXPORTLEN 1024
int main(int argc, char *argv[])
{
	char buf[0x10000];
	char port[MAXPORTLEN];
	int in, cnt;
	uint16_t start=0, end=0;
	static int hex, bin, eraseall, help_flag, scratch_flag;
	static struct option long_options[] = 
	{
		{"hex", no_argument, &hex, 1},
		{"bin", no_argument, &bin, 1},
		{"eraseall", no_argument, &eraseall, 'e'},
		{"scratch", no_argument, &scratch_flag, 'z'},
		{"port", required_argument, 0, 'p'},
		{"start", required_argument, 0, 's'},
		{"help", no_argument, &help_flag, 'h'},
		{0, 0, 0, 0}
	};
	int option_index = 0;
	int c, i;
	
	while(1)
	{
	 	c = getopt_long (argc, argv, "", long_options, &option_index);
		if( c==-1)
			break;
		switch(c)
		{
			case 0:		// set a flag, nothing to do
				break;
			case 'p':	// port
				printf("port = %s\n",optarg);
				strncpy( port, optarg, MAXPORTLEN );
				break;
			case 's':	// start address, for bin mode only
				start = strtoul( optarg, 0, 0);
			default:
				printf("unexpected option\n");
				break;
		}
	};
	if( help_flag || strlen(port)==0 )
	{
		help();
		return  help_flag ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if( bin && hex )
	{
		printf("ERROR :- you can either use binary or hex but not both!\n");
		return EXIT_FAILURE;
	}
	
	memset( buf, 0xFF, 0x10000 );	// 0xFF to match erased state fo flash memory
	ec2_connect( &ec2obj, port );
	if( eraseall )
	{
		printf("Erasing entire flash\n");
		ec2_erase_flash( &ec2obj );
	}
	
	if( hex )
	{
		if(start!=0)
		{
			printf("ERROR: You can't specify a start address when writing intel"
				   " hex files into the device\n");
			return EXIT_FAILURE;
		}
		// load all specified files into the buffer
		for( i = optind; i < argc; i++)
		{
			printf("i=%i\n",i);
			ihex_load_file( argv[i], buf, &start, &end );
		}
		printf("Writing to flash\n");
		if( scratch_flag )
			ec2_write_flash_scratchpad_merge( &ec2obj, &buf[start],
			                                  start, end-start+1 );
		else
			ec2_write_flash_auto_erase( &ec2obj, &buf[start], start,
			                            end-start+1 );
		printf("done\n");
	}
	
	if(bin)
	{
		if( (argc-optind)!=1)
		{
			printf("ERROR: binary mode only supports one file at a time\n");
			return EXIT_FAILURE;
		}
		in = open( argv[optind], O_RDONLY, 0);
		if( in )
		{
			cnt = read( in, buf, 0x10000 );
			printf("Writing %i bytes\n",cnt);
			if( scratch_flag )
			{
				if( (start+cnt) < 0x80 )
				{
					ec2_write_flash_scratchpad_merge( &ec2obj, buf,
					                                  start, cnt );
					printf("%i bytes written\n",cnt);
				}
				else
				{
					printf("Bin file too long, writing first %i bytes\n",
					       0x80-start);
					ec2_write_flash_scratchpad_merge( &ec2obj, buf,
					                                  start, 0x80-start );
				}
			}
			else
			{
				if( ec2_write_flash( &ec2obj, buf, start, cnt ) )
				{
					printf("%i bytes written successfully\n",cnt);
				}
				else
				{
					printf("Error: flash write failed\n");
					close( &ec2obj, in );
					return EXIT_FAILURE;
				}
			}
		}
		else
		{
			printf("Error: coulden't open %s\n",argv[2]);
			close( &ec2obj, in );
			return EXIT_FAILURE;
		}
		close( in );
	}
	ec2_disconnect( &ec2obj );
	return EXIT_SUCCESS;
}

