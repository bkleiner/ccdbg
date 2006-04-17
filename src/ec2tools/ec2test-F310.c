#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ec2drv.h"


// foward declarations
int test_data_ram();
int test_xdata_ram();
int test_flash();
int test_sfr();
int test_pc();
void print_buf( char *buf, int len );
void print_buf_dump( char *buf, int len );
EC2DRV obj;

int main(int argc, char *argv[])
{
	char buf[4096];
	int i;
	if( argc != 2 )
	{
		printf("ec2test-F310\nSyntax:\n\tec2test-F310 /dev/ttyS0\n");
		return EXIT_FAILURE;
	}
		
	obj.debug	= FALSE;
	//obj.mode	= C2;
	obj.mode	= AUTO;
	if( !ec2_connect( &obj, argv[1] ) )
		return -1;	// failure
#if 0
	BOOL bRunning=TRUE;
	// this is only useful if you have code in the micro at the time.
	printf("add breakpint 0x%x\n",ec2_addBreakpoint( &obj, 0x006A ));
//	printf("add breakpint 0x%x\n",ec2_addBreakpoint( &obj, 0x006B ));
	printf("add breakpint 0x%x\n",ec2_addBreakpoint( &obj, 0x006D ));
	//ec2_target_go();
	
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_target_run_bp( &obj, &bRunning );
	printf("PC = 0x%04X\n", ec2_read_pc( &obj ) );
	ec2_disconnect( &obj );
	return 0;
#endif
	printf("DATA  access test %s\n",test_data_ram()==0 ? "PASS":"FAIL");
	printf("XRAM access test %s\n",test_xdata_ram()==0 ? "PASS":"FAIL");
	printf("FLASH access test %s\n",test_flash()==0 ? "PASS":"FAIL");
	printf("PC access test %s\n",test_pc()==0 ? "PASS":"FAIL");
// SFR test commented out as some SFR's cause bad things to happen when poked,
// eg oscal etc
//	printf("SFR access test %s\n",test_sfr()==0 ? "PASS":"FAIL");
	ec2_disconnect( &obj );
	return EXIT_SUCCESS;
}


// returns number of failed tests
int test_data_ram()
{
	int fail = 0;
	char tbuf[256], rbuf[256];
	int i;
//	obj.debug = TRUE;
	printf("Testing dataram access\n");
	// write / read 0x00
	printf("\twrite / read 0x00\n");
	memset( tbuf, 0, sizeof(tbuf) );	
	ec2_write_ram( &obj, tbuf, 0, sizeof(tbuf) );
	memset( rbuf, 0xff, sizeof(rbuf) );
	ec2_read_ram( &obj, rbuf, 0, sizeof(rbuf) );
	if( memcmp( rbuf, tbuf, sizeof(rbuf) )!=0 )
	{
		printf("\tRW 0x00 FAILED\n");
		print_buf(tbuf,sizeof(tbuf));
		print_buf(rbuf,sizeof(rbuf));
		fail++;
	}
	printf("\twrite / read 0xff\n");
	memset( tbuf, 0xff, sizeof(tbuf) );	
	ec2_write_ram( &obj, tbuf, 0, sizeof(tbuf) );
	memset( rbuf, 0x00, sizeof(rbuf) );
	ec2_read_ram( &obj, rbuf, 0, sizeof(rbuf) );
	if( memcmp( rbuf, tbuf, sizeof(rbuf) )!=0 )
	{
		printf("\tRW 0xff FAILED\n");
		fail++;
	}
	
	printf("\twrite / read 0-ff sequence\n");
	for( i=0; i<=sizeof(tbuf); i++ )
		tbuf[i] = i;
	ec2_write_ram( &obj, tbuf, 0, sizeof(tbuf) );
	memset( rbuf, 0x00, sizeof(rbuf) );
	ec2_read_ram( &obj, rbuf, 0, sizeof(rbuf) );
	if( memcmp( rbuf, tbuf, sizeof(rbuf) )!=0 )
	{
		printf("\tRW 0-ff sequence FAILED\n");
		fail++;
		print_buf(rbuf,sizeof(rbuf));
	}

	printf("\tTesting RW random data\n");
	srand( time(0) );
	for( i=0; i<sizeof(tbuf); i++)
		tbuf[i] = rand() & 0x00FF;
	ec2_write_ram( &obj, tbuf, 0, sizeof(tbuf) );
	memset( rbuf, 0x00, sizeof(tbuf) );
	ec2_read_ram( &obj, rbuf, 0, sizeof(tbuf) );
	if( memcmp( rbuf, tbuf, sizeof(tbuf) )!=0 )
	{
		printf("\tRW random data FAILED\n");
		fail++;
		print_buf(rbuf,sizeof(tbuf));
	}

	printf("\tTesting RW mid ram write\n");
	// first blank out all RAM
	memset( tbuf, 0x0, 0x100 );
	ec2_write_ram( &obj, tbuf, 0x00, 0x100 );
	// now write in middle
	memset( tbuf, 0x55, 5  );
	ec2_write_ram( &obj, tbuf, 0x60, 5 );		// 10 bytes in middle of RAM
	// make tbuf what we expect
	memset( tbuf, 0x0, 0x100 );
	memset( tbuf+0x60, 0x55, 5  );
	ec2_read_ram( &obj, rbuf, 0x00, 0x100 );		// read entire RAM
	if( memcmp( rbuf, tbuf, 0x100 )!=0 )
	{
		printf("\tTesting RW mid ram write FAILED\n");
		fail++;
		print_buf(rbuf,0x100);
	}
	return fail;	
}


int test_xdata_ram()
{
	int fail = 0;
	char tbuf[1024], rbuf[1024];
	int i;

	printf("Testing xdata ram access\n");
	printf("\tTesting RW 0x00\n");
	memset( tbuf, 0x00, sizeof(tbuf) );
	memset( rbuf, 0xff, sizeof(tbuf) );
	ec2_write_xdata( &obj, tbuf, 0, sizeof(tbuf) );
	ec2_read_xdata( &obj, rbuf, 0, sizeof(rbuf) );
	if( memcmp( rbuf, tbuf, sizeof(rbuf) )!=0 )
	{
		printf("\tRW 0x00 FAILED\n");
		fail++;
		print_buf(rbuf,sizeof(rbuf));
	}
	
	printf("\tTesting RW 0xff\n");
	memset( tbuf, 0xff, sizeof(tbuf) );
	memset( rbuf, 0x00, sizeof(tbuf) );
	ec2_write_xdata( &obj, tbuf, 0, sizeof(tbuf) );
	ec2_read_xdata( &obj, rbuf, 0, sizeof(rbuf) );
	if( memcmp( rbuf, tbuf, sizeof(rbuf) )!=0 )
	{
		printf("\tRW 0xff FAILED\n");
		fail++;
		print_buf(rbuf,sizeof(rbuf));
	}

	printf("\tTesting RW 0x00-0xff sequence\n");
	for(i=0; i<=sizeof(tbuf); i++)
		tbuf[i] = i;
	memset( rbuf, 0x00, sizeof(tbuf) );
	ec2_write_xdata( &obj, tbuf, 0, sizeof(tbuf) );
	ec2_read_xdata( &obj, rbuf, 0, sizeof(rbuf) );
	if( memcmp( rbuf, tbuf, sizeof(rbuf) )!=0 )
	{
		printf("\tRW 0x00-0xff sequence FAILED\n");
		fail++;
		print_buf(rbuf,sizeof(rbuf));
	}

	printf("\tTesting RW mid page\n");
	// first blank out the page
	memset( tbuf, 0x0, 0x100 );
	ec2_write_xdata( &obj, tbuf, 0x0100, 0x100 );	// second page
	// now write in middle
	memset( tbuf, 0x55, 5  );
	ec2_write_xdata( &obj, tbuf, 0x010A, 5 );		// 10 bytes in in second page
	// make tbuf what we expect
	memset( tbuf, 0x0, 0x100 );
	memset( tbuf+0x0A, 0x55, 5  );
	ec2_read_xdata( &obj, rbuf, 0x100, 0x100 );		// read entire page
	if( memcmp( rbuf, tbuf, 0x100 )!=0 )
	{
		printf("\tRW mid page write FAILED\n");
		fail++;
		print_buf(rbuf,0x100);
	}
	printf("\tTesting RW random data\n");
	for( i=0; i<sizeof(tbuf); i++)
		tbuf[i] = rand() & 0x00FF;
	ec2_write_xdata( &obj, tbuf, 0, sizeof(tbuf) );
	memset( rbuf, 0x00, sizeof(tbuf) );
	ec2_read_xdata( &obj, rbuf, 0, sizeof(tbuf) );
	if( memcmp( rbuf, tbuf, sizeof(tbuf) )!=0 )
	{
		printf("\tRW random data FAILED\n");
		fail++;
		print_buf(rbuf,sizeof(tbuf));
	}
	return fail;
}

// It is dificult to test all Special function registers since some of them
// return different values for read than is written to them.
// some of them are not accessible for write due to Hardware limitations etc.
// this test is normally commented out but can be enabled for debugging
int test_sfr()
{
	int r = 0;
	unsigned int addr;
	char cw, cr;
	printf("SFR Access test:\n");
	for( addr = 0x80; addr<=0xFF; addr++ )
	{
		if( addr==0x87)	addr++;		// skip PCON
		cw = 0x55;
		ec2_write_sfr( &obj, &cw, addr );
		ec2_read_sfr( &obj, &cr, addr );
		if( cr != 0x55 )
		{
			r++;
			printf("\tSFR at addr = 0x%02x FAILED, read 0x%02x\n",addr,(unsigned char)cr);
		}
		else
			printf("\tSFR at addr = 0x%02x PASSED\n",addr);
	}
	return r;
}

// simple test of reading and writing the program counter
// assumes that processor has just been initialised
// giving the implicit indication that PC should be 0x0000 to begin with
int test_pc()
{
	int r=0;
	obj.debug=TRUE;
	printf("PC = 0x%04x, should be 0x0000\n",ec2_read_pc( &obj ));
	if( ec2_read_pc( &obj )!= 0x0000 )
		r++;
	ec2_set_pc( &obj, 0x1234 );
	printf("PC = 0x%04x, should be 0x1234\n",ec2_read_pc( &obj ));
	if( ec2_read_pc(&obj)!= 0x1234 )
		r++;
	ec2_set_pc( &obj, 0xabcd );
	printf("PC = 0x%04x, should be 0xabcd\n",ec2_read_pc( &obj ));
	if( ec2_read_pc(&obj)!= 0xabcd )
		r++;
	ec2_set_pc( &obj, 0x0000 );
	printf("PC = 0x%04x, should be 0x0000\n",ec2_read_pc( &obj ));
	if( ec2_read_pc(&obj)!= 0x0000 )
		r++;
	return r;
}

// test flash between 0x0000 and 0xFDFD
// ie all program memory less reserved ares
//  this test only works on processors with 64K flash
int test_flash()
{
	unsigned int r=0;
	unsigned int addr;
	char buf[0x4000];
	char rbuf[0x4000];
	
	printf("Beginning flash tests\n");
	ec2_erase_flash( &obj );
	ec2_read_flash( &obj, buf, 0x0000, 0x3dfe );
	for( addr=0; addr<0x3dfe; addr++ )
	{
		if( (buf[addr]) != (char)0xff )
		{
			printf("\tFlash erase/read back fail at addr = 0x%04x, data=0x%02x\n",addr,(unsigned char)buf[addr]);
			return -1;
			r++;
		}
	}
	printf("\tFlash erase pass\n");
	printf("\tWrite test, all flash, random\n");
	for( addr=0; addr<sizeof(buf); addr++ )
		buf[addr] = rand()&0x00FF;
	ec2_write_flash( &obj, buf, 0x0000, 0x3dfe );
	ec2_read_flash( &obj, rbuf, 0x0000, 0x3dfe );
	if( memcmp( buf, rbuf, 0x3dfe )==0 )
		printf("\tPASS\n");
	else
	{
		printf("\tFAIL\n");
		// dump what was written
		printf("write:\n");
		print_buf_dump( buf, 0xfdfe ); 
		// dump the read memory
		printf("read:\n");
		print_buf_dump( rbuf, 0xfdfe ); 
		r++;
		return r;	// hack for debug
	}

	ec2_erase_flash( &obj );
	// write test
	printf("\tFlash write random block\n");
	for( addr=0; addr<sizeof(buf); addr++ )
		buf[addr] = rand()&0x00FF;
	ec2_write_flash( &obj, buf, 0x0010, 0x00E0 );
	ec2_read_flash( &obj, rbuf, 0x0010, 0x00E0 );
	if( memcmp( buf, rbuf, 0x00E0 )==0 )
		printf("\tPASS\n");
	else
	{
		printf("\tFAIL\n");
		r++;
	}
	
	// write test
	printf("\tFlash write another random block, auto erase\n");
	for( addr=0; addr<sizeof(buf); addr++ )
		buf[addr] = rand()&0x00FF;
	ec2_write_flash_auto_erase( &obj, buf, 0x1567, 0x0123 );
	ec2_read_flash( &obj, rbuf, 0x1567, 0x0123 );
	if( memcmp( buf, rbuf, 0x0123 )==0 )
		printf("\tPASS\n");
	else
	{
		printf("\tFAIL\n");
		r++;
	}
	
	// write test
	printf("\tFlash write another random block, auto keep\n");
	for( addr=0; addr<sizeof(buf); addr++ )
		buf[addr] = rand()&0x00FF;
	ec2_write_flash_auto_keep( &obj, buf, 0x1367, 0x0500 );
	ec2_read_flash( &obj, rbuf, 0x1367, 0x0500 );
	if( memcmp( buf, rbuf, 0x0123 )==0 )
		printf("\tPASS\n");
	else
	{
		printf("\tFAIL\n");
		r++;
	}

	printf("\tWrite test, all flash, random, auto erase\n");
	for( addr=0; addr<sizeof(buf); addr++ )
		buf[addr] = rand()&0x00FF;

	// highest address of flash usable for user program is 0xfffd hence a length of ffde
	ec2_write_flash_auto_erase( &obj, buf, 0x0000, 0x3dfe );
	ec2_read_flash( &obj, rbuf, 0x0000, 0x3dfe );
	if( memcmp( buf, rbuf, 0x3dfe )==0 )
		printf("\tPASS\n");
	else
	{
		printf("\tFAIL\n");
		r++;
	}

	printf("\tErasing flash\n");
	ec2_erase_flash( &obj );
	
	ec2_disconnect( &obj );
	return r;
}



void print_buf( char *buf, int len )
{
	while( len-- !=0 )
		printf("%02X ",(unsigned char)*buf++);
	printf("\n");
}


void print_buf_dump( char *buf, int len )
{
	const PerLine = 16;
	int i, addr;

	for( addr=0; addr<len; addr += PerLine )
	{
		printf("%04x\t",addr);
		// print each hex byte		
		for( i=0; i<PerLine; i++ )
			printf("%02x ",buf[addr+i]);
		printf("\t");
		for( i=0; i<PerLine; i++ )
			putchar( (buf[addr+i]>='0' && buf[addr+i]<='z') ? buf[addr+i] : '.' );
		putchar('\n');
	}
}
