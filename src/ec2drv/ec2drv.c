/** EC2 Driver Library
  *
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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>			// UNIX standard function definitions
#include <fcntl.h>			// File control definitions
#include <errno.h>			// Error number definitions
#include <termios.h>		// POSIX terminal control definitions
#include <sys/ioctl.h>
#include "ec2drv.h"
#include "config.h"

#include <usb.h>			// Libusb header
#include <sys/ioctl.h>

#define MAJOR_VER 0
#define MINOR_VER 4

#define MIN_EC2_VER 0x12	///< Minimum usable EC2 Firmware version
#define MAX_EC2_VER 0x13	///< Highest tested EC2 Firmware version, will try and run with newer versions
#define MIN_EC3_VER 0x07	///< Minimum usable EC3 Firmware version
#define MAX_EC3_VER 0x0a	///< Highest tested EC3 Firmware version, will try and run with newer versions

//#define FUNC_TRACE
#ifdef FUNC_TRACE
	#define DUMP_FUNC()		printf("Function = %s\n",__PRETTY_FUNCTION__ );
#define DUMP_FUNC_END()	printf("End Function = %s\n",__PRETTY_FUNCTION__ );
#else
	#define DUMP_FUNC()
#define DUMP_FUNC_END()
#endif	


#define SFR_PAGE_REG	0x84		///< SFR Page selection register




/** Retrieve the ec2drv library version
  * \returns the version.  upper byte is major version, lower byte is minor
  */
uint16_t ec2drv_version()
{
	return (MAJOR_VER<<8) | MINOR_VER;
}

typedef struct
{
	char *tx;
	int tlen;
	char *rx;
	int rlen;
} EC2BLOCK;

// Internal functions
static void	init_ec2( EC2DRV *obj );
static BOOL	txblock( EC2DRV *obj, EC2BLOCK *blk );
static BOOL	trx( EC2DRV *obj, char *txbuf, int txlen, char *rxexpect, int rxlen );
static void	print_buf( char *buf, int len );
static int	getNextBPIdx( EC2DRV *obj );
static int	getBP( EC2DRV *obj, uint16_t addr );
static BOOL	setBpMask( EC2DRV *obj, int bp, BOOL active );
static void set_flash_addr_jtag( EC2DRV *obj, int16_t addr );
inline static void update_progress( EC2DRV *obj, uint8_t percent );
static uint8_t sfr_fixup( uint8_t addr );
BOOL ec2_write_flash_c2( EC2DRV *obj, char *buf, int start_addr, int len );
BOOL ec2_write_flash_jtag( EC2DRV *obj, char *buf, int start_addr, int len );
uint16_t device_id( EC2DRV *obj );
void write_breakpoints_c2( EC2DRV *obj );
BOOL ec2_connect_jtag( EC2DRV *obj, const char *port );


static void ec2_read_xdata_c2_emif( EC2DRV *obj, char *buf, int start_addr, int len );
static BOOL ec2_write_xdata_c2_emif( EC2DRV *obj, char *buf, int start_addr, int len );


// PORT support
static BOOL open_port( EC2DRV *obj, const char *port );
static BOOL write_port_ch( EC2DRV *obj, char ch );
static BOOL write_port( EC2DRV *obj, char *buf, int len );
static int read_port_ch( EC2DRV *obj );
static BOOL read_port( EC2DRV *obj, char *buf, int len );
static void rx_flush( EC2DRV *obj );
static void tx_flush( EC2DRV *obj );
static void close_port( EC2DRV *obj );
static void DTR( EC2DRV *obj, BOOL on );
static void RTS( EC2DRV *obj, BOOL on );


static BOOL open_ec3( EC2DRV *obj, const char *port );
static void close_ec3( EC2DRV *obj );
static BOOL write_usb( EC2DRV *obj, char *buf, int len );
static BOOL write_usb_ch( EC2DRV *obj, char ch );
static BOOL read_usb( EC2DRV *obj, char *buf, int len );
static int read_usb_ch( EC2DRV *obj );
void close_ec3( EC2DRV *obj );


/** Connect to the EC2/EC3 device.
  *
  * This will perform any initialisation required to bring the device into
  * an active state.
  * this function must be called before any other operation
  *
  * \param port name of the linux device the EC2 is connected to, eg "/dev/ttyS0"
  *				or "/dev/ttyUSB0" for an EC2 on a USB-serial converter
  *				or USB for an EC3, or to specify an exact device USB::XXXXXXXX
  *				where XXXXXXXX is the device serial number.
  * \returns TRUE on success
  */
BOOL ec2_connect( EC2DRV *obj, const char *port )
{
	DUMP_FUNC();
	int ec2_sw_ver;
	const char *lport = port;
	uint16_t idrev;
	strncpy( obj->port, port, sizeof(obj->port) );
	
	if( obj->mode == AUTO )
	{
		printf(	"*********************************************************************\n"
				"* WARNING: Auto detection of mode may cause initialisation sequence *\n"
				"* to differ significantly from the SiLabs IDE.                      *\n"
				"* In the case of problems specify --mode=C2 or --mode=JTAG          *\n"
				"*********************************************************************\n\n");
	}
	
	obj->progress = 0;
	obj->progress_cbk = 0;
	if( strncmp(lport,"USB",3)==0 )
	{
		// USB mode, EC3
		obj->dbg_adaptor = EC3;
		if( lport[3]==':' )
		{
			lport = lport+4;	// point to the remainder ( hopefully the serial number of the adaptor )
		}
		else if( strlen(lport)==3 )
		{
			lport = 0;
		}
		else
			return FALSE;
	}
	else
		obj->dbg_adaptor = EC2;
	
	
	if( !open_port( obj, lport) )
	{
		printf("Coulden't connect to %s\n", obj->dbg_adaptor==EC2 ? "EC2" : "EC3");
		return FALSE;
	}

	// call new jtag init
	if(obj->mode==JTAG)
		return ec2_connect_jtag( obj, port );

	
	ec2_reset( obj );
	if( obj->dbg_adaptor==EC2 )
	{
		if( !trx( obj,"\x55",1,"\x5A",1 ) )
			return FALSE;
		if( !trx( obj,"\x00\x00\x00",3,"\x03",1) )
			return FALSE;
		if( !trx( obj,"\x01\x03\x00",3,"\x00",1) )
			return FALSE;
	} 
	else if( obj->dbg_adaptor==EC3 )
	{
		if( !trx( obj,"\x00\x00\x00",3,"\x02",1) )
			return FALSE;
		if( !trx( obj,"\x01\x0c\x00",3,"\x00",1) )
			return FALSE;
	}
	
	write_port( obj,"\x06\x00\x00",3);
	ec2_sw_ver = read_port_ch( obj );
	if( obj->dbg_adaptor==EC2 )
	{
		printf("EC2 firmware version = 0x%02x\n",ec2_sw_ver);
		//if( ec2_sw_ver != 0x12  && ec2_sw_ver != 0x13 && ec2_sw_ver != 0x14)
		//{
		//	printf("Incompatible EC2 firmware version, version 0x12 required\n");
		//	return FALSE;
		//}
		if( ec2_sw_ver < MIN_EC2_VER )
		{
			printf("Incompatible EC2 firmware version,\n"
					"Versions between 0x%02x and 0x%02x inclusive are reccomended\n"
					"Newer vesrions may also be tried and will just output a warning that they are untested\n", MIN_EC2_VER, MAX_EC2_VER);
		}
		else if( ec2_sw_ver > MAX_EC2_VER )
		{
			printf("Warning: this version is newer than the versions tested by the developers,\n");
			printf("Please report success / failure and version via ec2drv.sf.net\n");
		}
	}
	else if( obj->dbg_adaptor==EC3 )
	{
		printf("EC3 firmware version = 0x%02x\n",ec2_sw_ver);
		if( ec2_sw_ver < MIN_EC3_VER )
		{
			printf("Incompatible EC3 firmware version,\n"
					"Versions between 0x%02x and 0x%02x inclusive are reccomended\n"
					"Newer vesrions may also be tried and will just output a warning that they are untested\n", MIN_EC2_VER, MAX_EC2_VER);
		}
		else if( ec2_sw_ver > MAX_EC3_VER )
		{
			printf("Warning: this version is newer than the versions tested by the developers,\n");
			printf("Please report success / failure and version via ec2drv.sf.net\n");
		}
	}
	
	if( obj->mode==AUTO )
	{
		// try and figure out what communication the connected device uses
		// JTAG or C2
		idrev=0;
		obj->mode=C2;
		trx(obj, "\x20",1,"\x0D",1);	// select C2 mode
		idrev = device_id( obj );
		if( idrev==0xffff )
		{
			obj->mode = JTAG;			// device most probably a JTAG device
			// On EC3 the simplistic mode change dosen't work,
			// we take the slower approach and restart the entire connection.
			// This seems the most reliable method.
			// If you find it too slow just specify the mode rather than using auto.
//			if( obj->dbg_adaptor==EC3 )
//			{
				printf("NOT C2, Trying JTAG\n");
				ec2_disconnect( obj );
				ec2_connect( obj, obj->port );
				return TRUE;
//			}
//			trx(obj, "\x04",1,"\x0D",1);	// select JTAG mode
//			idrev = device_id( obj );
//			ec2_connect_jtag( obj, obj->port );
#if 0
			if( idrev==0xFF00 )
			{
				printf("ERROR :- Debug adaptor Not connected to a microprocessor\n");
				ec2_disconnect( obj );
				exit(-1);
			}
#endif
		}
	}
	else
	{
		if(obj->mode==JTAG)
		{
			trx(obj, "\x04",1,"\x0D",1);	// select JTAG mode
		}
		else if(obj->mode==C2)
			trx(obj, "\x20",1,"\x0D",1);	// select C2 mode
		idrev = device_id( obj );
		if( idrev==0xFF00 || idrev==0xFFFF )
		{
			printf("ERROR :- Debug adaptor Not connected to a microprocessor\n");
			ec2_disconnect( obj );
			exit(-1);
		}
		obj->dev = getDevice( idrev>>8, idrev&0xFF );
		obj->dev = getDeviceUnique( unique_device_id(obj), 0);
		ec2_target_reset( obj );
		return TRUE;
	}
	obj->dev = getDevice( idrev>>8, idrev&0xFF );
	obj->dev = getDeviceUnique( unique_device_id(obj), 0);
//	init_ec2(); Does slightly more than ec2_target_reset() but dosen't seem necessary
	ec2_target_reset( obj );
	return TRUE;
}

/** new JTAG connect function
	will be called by a common auto-detect function
	This split has been to do simplify debugging
*/
BOOL ec2_connect_jtag( EC2DRV *obj, const char *port )
{
	DUMP_FUNC()
	char buf[32];
	// adaptor version
	ec2_reset( obj );
	if( obj->dbg_adaptor==EC2 )
	{
		if( !trx( obj,"\x55",1,"\x5A",1 ) )
			return FALSE;
		if( !trx( obj,"\x00\x00\x00",3,"\x03",1) )
			return FALSE;
		if( !trx( obj,"\x01\x03\x00",3,"\x00",1) )
			return FALSE;
	} 
	else if( obj->dbg_adaptor==EC3 )
	{
		if( !trx( obj,"\x00\x00\x00",3,"\x02",1) )
			return FALSE;
		if( !trx( obj,"\x01\x0c\x00",3,"\x00",1) )
			return FALSE;
	}
	write_port( obj, "\x06\x00\x00",3);
	read_port( obj, buf, 1);
	
	printf("Debug adaptor ver = 0x%02x\n",buf[0]);
	ec2_target_reset( obj );
	obj->dev = getDeviceUnique( unique_device_id(obj), 0);
	
	DUMP_FUNC_END();
	return TRUE;
}


BOOL ec2_connect_fw_update( EC2DRV *obj, char *port )
{
	DUMP_FUNC();

	obj->progress = 0;
	obj->progress_cbk = 0;
	if( strncmp(port,"USB",3)==0 )
	{
		// USB mode, EC3
		obj->dbg_adaptor = EC3;
		if( port[3]==':' )
		{
			// get the rest of the string
			port = port+4;	// point to the remainder ( hopefully the serial number of the adaptor )
		}
		else if( strlen(port)==3 )
		{
			port = 0;
		}
		else
			return FALSE;
	}
	else
		obj->dbg_adaptor = EC2;
	
	
	if( !open_port( obj, port) )
	{
		printf("Coulden't connect to %s\n", obj->dbg_adaptor==EC2 ? "EC2" : "EC3");
		return FALSE;
	}
	DUMP_FUNC_END();
	return TRUE;
}


// identify the device, id = upper 8 bites, rev = lower 9 bits
uint16_t device_id( EC2DRV *obj )
{
	DUMP_FUNC();
	char buf[6];
	if( obj->mode==C2 )
	{
// this appeared in new versions of IDE but seems to have no effect for F310	
// EC2 chokes on this!!!!		trx(obj,"\xfe\x08",2,"\x0d",1);
		write_port( obj,"\x22", 1 );	// request device id (C2 mode)
		read_port( obj, buf, 2 );
		return buf[0]<<8 | buf[1];
	}
	else if( obj->mode==JTAG )
	{
//		trx( obj,"\x0A\x00",2,"\x21\x01\x03\x00\x00\x12",6);	
		write_port( obj, "\x0A\x00", 2 );
		read_port( obj, buf, 6 );
		return buf[2]<<8 | 0;	// no rev id known yet
	}
	return 0;	// Invalid mode.
}


// identify the device, id = upper 8 bites, rev = lower 9 bits
uint16_t unique_device_id( EC2DRV *obj )
{
	DUMP_FUNC();
	char buf[40];
	if( obj->mode==C2 )
	{
//		ec2_target_halt(obj);	// halt needed otherwise device may return garbage!
		write_port(obj,"\x23",1);
		read_port(obj,buf,3);
		print_buf( buf,3);
//		ec2_target_halt(obj);	// halt needed otherwise device may return garbage!
		
		// test code
		trx(obj,"\x2E\x00\x00\x01",4,"\x02\x0D",2);
		trx(obj,"\x2E\xFF\x3D\x01",4,"xFF",1);
		
		return buf[1];
	}
	else if( obj->mode==JTAG )
	{
//		trx(obj,"\x16\x01\xE0",3,"\x00",1);	// test
// why 15/10/06?		trx(obj,"\x0b\x02\x02\x00",4,"\x0D",1);	// sys reset
		trx(obj,"\x0b\x02\x02\x00",4,"\x0D",1);	// sys reset	Makes system halt when required.
		ec2_target_halt(obj);	// halt needed otherwise device may return garbage!
		trx(obj,"\x10\x00",2,"\x07\x0D",2);
		write_port(obj,"\x0C\x02\x80\x12",4);
		read_port(obj,buf,4);
//		print_buf( buf,4);
		return buf[2];
	}
	return -1;
}

/** Disconnect from the EC2/EC3 releasing the serial port.
	This must be called before the program using ec2drv exits, especially for
	the EC3 as exiting will leave the device in an indeterminant state where
	it may not respond correctly to the next application that tries to use it.
	software retries or replugging the device may bring it back but it is 
	definitly prefered that this function be called.
*/
void ec2_disconnect( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->dbg_adaptor==EC3)
	{
		char buf[255];
		int r;

		r = trx( obj, "\x21", 1, "\x0d", 1 );
		usb_control_msg( obj->ec3, USB_TYPE_CLASS + USB_RECIP_INTERFACE, 0x9, 0x340, 0,"\x40\x02\x0d\x0d", 4, 1000);
		r = usb_interrupt_read(obj->ec3, 0x00000081, buf, 0x0000040, 1000);
		
		r = usb_release_interface(obj->ec3, 0);
		assert(r == 0);
		usb_reset( obj->ec3);
		r = usb_close( obj->ec3);
		assert(r == 0);
		DUMP_FUNC_END();
		return;
	}
	else if( obj->dbg_adaptor==EC2)
	{
		DTR( obj, FALSE );
	}
	close_port( obj );
	DUMP_FUNC_END();
}


/** Translates certian special SFR addresses for read and write 
  *  reading or writing the sfr address as per datasheet returns incorrect
  * information.
  * These mappings seem necessary due to the way the hardware is implemented.
  *  The access is the same byte sequence as a normal SFR but the address is
  * much lower starting arround 0x20.
  */
static uint8_t sfr_fixup( uint8_t addr )
{
	DUMP_FUNC();
	switch( addr )
	{
		case 0xD0:	return 0x23;	// PSW
		case 0xE0:	return 0x22;	// ACC
		default:	return addr;
	}
}


/** SFR read command							<br>
  * T 02 02 addr len							<br>
  * len <= 0x0C									<br>
  * addr = SFR address 0x80 - 0xFF				<br>
  *
  * \param buf buffer to store the read data
  * \param addr address to read from, must be in SFR area, eg 0x80 - 0xFF
  */
void ec2_read_sfr( EC2DRV *obj, char *buf, uint8_t addr )
{
	DUMP_FUNC();
	assert( addr >= 0x80 );
	ec2_read_ram_sfr( obj, buf, sfr_fixup( addr ), 1, TRUE );
	DUMP_FUNC_END();
}

/** write to an SFR (Special Function Register)
  * NOTE some SFR's appear to accept writes but do not take any action on the
  * heardware.  This seems to be the same SFRs that the SI labs IDE can't make
  * change either.
  *
  * One possible work arroud is to place a couple of byte program in the top of
  * flash and then the CPU state can be saved (via EC2) and then values poked 
  * into regs and this code stepped through.  This would mean we could change 
  * any sfr provided the user application can spare a few bytes of code memory
  * The SFR's that don't write correctly are a subset of the bit addressable ones
  * for some of them the SI labs IDE uses a different command.
  * This function will add support for knowen alternative access methods as found.
  *
  * \param buf buffer containing data to write
  * \param addr sfr address to begin writing at, must be in SFR area, eg 0x80 - 0xFF
  * \param len Number of bytes to write.
  */
void ec2_write_sfr( EC2DRV *obj, uint8_t value, uint8_t addr )
{
	DUMP_FUNC();
	char cmd[4];
	assert( addr >= 0x80 );

	if( obj->mode==JTAG )
	{
		cmd[0] = 0x03;
		cmd[1] = 0x02;
		cmd[2] = sfr_fixup( addr );
		cmd[3] = value;
		trx( obj,cmd,4,"\x0D",1 );
	}
	else if( obj->mode==C2 )
	{
		cmd[0] = 0x29;
		cmd[1] = sfr_fixup( addr );
		cmd[2] = 0x01;
		cmd[3] = value;
		trx( obj,cmd,4,"\x0D",1 );
	}
	DUMP_FUNC_END();
}



////////////////////////////////////////////////////////////////////////////////
// Pages SFR Support
////////////////////////////////////////////////////////////////////////////////

/**	Read a paged Special function register.
	\param[in]	obj		EC2 object to operate on
	\param[in]	page	Page number the register resides on (0-255)
	\param[in]	addr	Register address (0x80-0xFF)
	\param[out]	ok		if non zero on return will indicate success/failure
	\returns			value read from the register
*/
uint8_t ec2_read_paged_sfr( EC2DRV *obj, uint8_t page, uint8_t addr, BOOL *ok )
{
	uint8_t cur_page, value;
	
	// Save page register
	if( obj->dev->has_paged_sfr )
		cur_page = ec2_read_raw_sfr( obj, SFR_PAGE_REG, 0 );
	
	value = ec2_read_raw_sfr( obj, addr, ok );
	
	// Restore page register
	if( obj->dev->has_paged_sfr )
		ec2_write_raw_sfr( obj, SFR_PAGE_REG, cur_page );

	return value;
}

/**	Write to  a paged Special function register.
	\param[in]	obj		EC2 object to operate on
	\param[in]	page	Page number the register resides on (0-255)
	\param[in]	addr	Register address (0x80-0xFF)
	\param[in]	value	Value to write to register (0-0xff)
	\returns			TRUE on success, FALSE on addr out of range
 */
BOOL ec2_write_paged_sfr(EC2DRV *obj, uint8_t page, uint8_t addr, uint8_t value)
{
	uint8_t cur_page;
	BOOL result;
	
	// Save page register
	if( obj->dev->has_paged_sfr )
		cur_page = ec2_read_raw_sfr( obj, SFR_PAGE_REG, 0 );

	result = ec2_write_raw_sfr( obj, addr, value );
	
	// Restore page register
	if( obj->dev->has_paged_sfr )
		ec2_write_raw_sfr( obj, SFR_PAGE_REG, cur_page );
	
	return result;
}


/**	Read a Special function register from the current page
	\param[in]	obj		EC2 object to operate on
	\param[in]	addr	Register address (0x80-0xFF)
	\param[out]	ok		if non zero on return will indicate success/failure
	\returns			value read from the register
 */
uint8_t ec2_read_raw_sfr( EC2DRV *obj, uint8_t addr, BOOL *ok )
{
	uint8_t value;
	if( addr>=0x80 )
	{
		ec2_read_sfr( obj, (char*)&value, addr );
		*ok = TRUE;
	}
	else
	{
		value = 0;
		*ok = FALSE;
	}
	return value;
}

/**	Write to a Special function register in the current page
	\param[in]	obj		EC2 object to operate on
	\param[in]	addr	Register address (0x80-0xFF)
	\param[in]	value	Value to write to register (0-0xff)
	\returns			TRUE on success, FALSE on addr out of range
 */
BOOL ec2_write_raw_sfr( EC2DRV *obj, uint8_t addr, uint8_t value )
{
	if( addr>=0x80 )
	{
		ec2_write_sfr( obj, value, addr );
		return TRUE;
	}
	return FALSE;
}



/** Read ram
  * Read data from the internal data memory
  * \param buf buffer to store the read data
  * \param start_addr address to begin reading from, 0x00 - 0xFF
  * \param len Number of bytes to read, 0x00 - 0xFF
  */
void ec2_read_ram( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();

	// special case here and call 
	if( obj->mode == JTAG )
	{
		ec2_read_ram_sfr( obj, buf, start_addr, len, FALSE );	
		char tmp[4];
		write_port( obj,"\x02\x02\x24\x02",4 );
		read_port( obj, &tmp[0], 2);
		usleep(10000);
		write_port( obj,"\x02\x02\x26\x02",4 );
		read_port( obj, &tmp[2], 2);
		usleep(10000);
		if( start_addr<3 )
		{
			memcpy( &buf[0], &tmp[start_addr], 3-start_addr );
		}
	}
	else if( obj->mode==C2 )
	{
		char tmp[4];
		ec2_read_ram_sfr( obj, buf, start_addr, len, FALSE );
		/// \TODO we need to do similar to above, need to check out if there is a generic way to handle
		/// the first 2 locations,  should be since the same reason the the special case and sfr reads
		/// are already JTAG / C2 aware.
		// special case, read first 3 bytes of ram
		//T 28 24 02		R 7C 00
		//T 28 26 02		R 00 00
		write_port( obj,"\x28\x24\x02",3 );
		read_port( obj, &tmp[0], 2);
		write_port( obj,"\x28\x26\x02",3 );
		read_port( obj, &tmp[2], 2);
		if( start_addr<3 )
		{
			memcpy( &buf[0], &tmp[start_addr], 3-start_addr );
		}
	}
	DUMP_FUNC_END();
}


/** Read ram or sfr
  * Read data from the internal data memory or from the SFR region
  * \param buf buffer to store the read data
  * \param start_addr address to begin reading from, 0x00 - 0xFF
  * \param len Number of bytes to read, 0x00 - 0xFF
  * \param sfr TRUE if you want to read a special function register, FALSE to read RAM
  */
void ec2_read_ram_sfr( EC2DRV *obj, char *buf, int start_addr, int len, BOOL sfr )
{
	DUMP_FUNC();
	int i;
	char cmd[0x40];
	if( !((int)start_addr+len-1 <= 0xFF))
		printf("void ec2_read_ram_sfr( EC2DRV *obj, char *buf, 0x%02x, 0x%04x, %u )",
		start_addr, len, sfr );
	assert( (int)start_addr+len-1 <= 0xFF );	// RW -1 to allow reading 1 byte at 0xFF

	if( obj->mode == JTAG )
	{
		memset( buf, 0xff, len );	
		cmd[0] = sfr ? 0x02 : 0x06;
		cmd[1] = 0x02;
		for( i = 0; i<len; i+=0x0C )
		{
			cmd[2] = start_addr+i;
			cmd[3] = len-i >= 0x0C ? 0x0C : len-i;
			write_port( obj, cmd, 0x04 );
			usleep(10000);	// try to prevent bad reads of RAM by letting the EC2 take a breather
			read_port( obj, buf+i, cmd[3] );
		}
	}	// End JTAG
	else if( obj->mode == C2 )
	{
		char block_len = obj->dbg_adaptor==EC2 ? 0x0c : 0x3b;
		memset( buf, 0xff, len );
		cmd[0] = sfr ? 0x28 : 0x2A;		// SFR read or RAM read
		for( i = 0; i<len; i+=block_len )
		{
			cmd[1] = start_addr+i;
			cmd[2] = len-i >= block_len ? block_len : len-i;
			write_port( obj, cmd, 3 );
			read_port( obj, buf+i, cmd[2] );
		}
	}	// End C2
	DUMP_FUNC_END();
}

/** Write data into the micros RAM					<br>
  * cmd  07 addr len a b							<br>
  * len is 1 or 2									<br>
  * addr is micros data ram location				<br>
  * a  = first databyte to write					<br>
  * b = second databyte to write					<br>
  *
  * @todo take improvments for C2 mode and apply to JTAG mode,  factor out common code
  *
  * \param buf buffer containing dsata to write to data ram
  * \param start_addr address to begin writing at, 0x00 - 0xFF
  * \param len Number of bytes to write, 0x00 - 0xFF
  *
  * \returns TRUE on success, otherwise FALSE
  */
BOOL ec2_write_ram( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	int i, blen;
	char cmd[5], tmp[2];
	assert( start_addr>=0 && start_addr<=0xFF );
	// printf("start addr = 0x%02x\n",start_addr);
	if( obj->mode == JTAG )
	{
		// special case the first 2 bytes of RAM
		i=0;
		while( (start_addr+i)<3 && ((len-i)>=1) )
		{
			cmd[0] = 0x03;
			cmd[1] = 0x02;
			cmd[2] = 0x24+start_addr+i;
			cmd[3] = buf[i];
			trx( obj, cmd, 4, "\x0D", 1 );
			// printf("write special addr=0x%04x, data=0x%02x\n",(unsigned char)start_addr+i,(unsigned char)buf[i]);
			i++;
		}

		for( ; i<len; i+=2 )
		{
			cmd[0] = 0x07;
			cmd[1] = start_addr + i;
			blen = len-i;
			if( blen>=2 )
			{
				cmd[2] = 0x02;		// two bytes
				cmd[3] = buf[i];
				cmd[4] = buf[i+1];
				trx( obj, cmd, 5, "\x0d", 1 );
			}
			else
			{
				// single byte write but ec2 only does 2 byte writes correctly.
				// we read the affected bytes and change the first to our desired value
				// then write back
				if( (start_addr + i) < 0xff )
				{
					cmd[0] = 0x07;
					cmd[1] = start_addr + i;
					cmd[2] = 0x02;			// two bytes
					ec2_read_ram( obj, &cmd[3], start_addr+i, 2 );
					cmd[3] = buf[i];		// poke in desired value
					trx( obj, cmd, 5, "\x0d", 1 );
				}
				else
				{
					// expirimental
					cmd[0] = 0x07;
					cmd[1] = start_addr + i-1;
					cmd[2] = 0x02;			// two bytes
					ec2_read_ram( obj, &cmd[3], start_addr+i-1, 2 );
					cmd[4] = buf[i];		// poke in desired value
					trx( obj, cmd, 5, "\x0d", 1 );
				}
				// FIXME seems to be broken if we want to modify the byte at 0xff
			}
		}
	}	// End JTAG mode
	else if( obj->mode == C2 )
	{
		// special case for first 2 bytes, related to R0 / R1 I think
		// special case the first 2 bytes of RAM
		// looks like it should be the first 3 bytes
		i=0;
		while( (start_addr+i)<=0x02 && ((len-i)>=1) )
		{
			cmd[0] = 0x29;
			cmd[1] = 0x24+start_addr+i;
			cmd[2] = 0x01;	// len
			cmd[3] = buf[i];
			trx( obj, cmd, 4, "\x0D", 1 );
			i++;
		}
		// normal writes
		// 0x2b 0x03 0x02 0x55 0x55
		//  |    |    |    |    |
		//	|    |    |    |    +--- Seconds data byte
		//	|    |    |    +-------- Seconds data byte
		//	|    |    +------------- Number of bytes, must use 2 using 1 or >2 won't work!
		//	|    +------------------ Start address
		//	+----------------------- Data ram write command
		for( ; i<len; i+=2 )
		{
			cmd[0] = 0x2b;				// write data RAM
			cmd[1] = start_addr + i;	// address
			blen = len-i;
			if( blen>=2 )
			{
				cmd[2] = 0x02;			// two bytes
				cmd[3] = buf[i];
				cmd[4] = buf[i+1];
				trx( obj, cmd, 5, "\x0d", 1 );
			}
			else
			{
				// read back, poke in byte and call write for 2 bytes
				if( (start_addr+i) == 0xFF )
				{
					// must use previous byte
					ec2_read_ram( obj, tmp, start_addr+i-1, 2 );
					tmp[1] = buf[i];	// databyte to write at 0xFF
					ec2_write_ram( obj, tmp, start_addr+i-1, 2 );
				}
				else
				{
					// use following byte
					ec2_read_ram( obj, tmp, start_addr+i, 2 );
					tmp[0] = buf[i];	// databyte to write
					ec2_write_ram( obj, tmp, start_addr+i, 2 );
				}
			}
		}
	}	// End C2 Mode
	DUMP_FUNC_END();
	return TRUE;
}

/** write to targets XDATA address space			<BR>
  * Preamble... trx("\x03\x02\x2D\x01",4,"\x0D",1);	<BR>
  * <BR>
  * Select page address:							<BR>
  * trx("\x03\x02\x32\x00",4,"\x0D",1);				<BR>
  * cmd: 03 02 32 addrH								<BR>
  * where addrH is the top 8 bits of the address	<BR>
  * cmd : 07 addrL len a b							<BR>
  * addrL is low byte of address					<BR>
  * len is 1 of 2									<BR>
  * a is first byte to write						<BR>
  * b is second byte to write						<BR>
  * <BR>
  * closing :										<BR>
  * cmd 03 02 2D 00									<BR>
  *
  * \param buf buffer containing data to write to XDATA
  * \param start_addr address to begin writing at, 0x00 - 0xFFFF
  * \param len Number of bytes to write, 0x00 - 0xFFFF
  *
  * \returns TRUE on success, otherwise FALSE
  */
BOOL ec2_write_xdata( EC2DRV *obj, char *buf, int start_addr, int len )
{ 
	DUMP_FUNC();
	if( obj->mode==JTAG )
	{
		int blen, page;
		char start_page	= ( start_addr >> 8 ) & 0xFF;
		char last_page	= ( (start_addr+len-1) >> 8 ) & 0xFF;
		unsigned int ofs=0;

		unsigned int pg_start_addr, pg_end_addr;	// start and end addresses within page
		assert( start_addr>=0 && start_addr<=0xFFFF && start_addr+len<=0x10000 );
		
		for( page = start_page; page<=last_page; page++ )
		{
			pg_start_addr = (page==start_page) ? start_addr&0x00FF : 0x00;	
			pg_end_addr = (page==last_page) ? (start_addr+len-1)-(page<<8) : 0xff;
			blen = pg_end_addr - pg_start_addr + 1;	
	//		printf("page = 0x%02x, start = 0x%04x, end = 0x%04x, len = %i, ofs=%04x\n", page,pg_start_addr, pg_end_addr,blen,ofs);
			ec2_write_xdata_page( obj, buf+ofs, page, pg_start_addr, blen );
			ofs += blen;
		}
	}	// End JTAG
	else if( obj->mode==C2 && obj->dev->has_external_bus)
	{
		return ec2_write_xdata_c2_emif( obj, buf, start_addr, len );
	}
	else if( obj->mode==C2 && !obj->dev->has_external_bus)
	{
		// T 29 ad 01 00	R 0d
		// T 29 c7 01 00	R 0d
		// T 29 84 01 55	R 0d	// write 1 byte (0x55) at the current address then increment that addr
		char cmd[4];
		unsigned int i;
		// low byte of start address
		cmd[0] = 0x29;
		cmd[1] = 0xad;
		cmd[2] = 0x01;		// length
		cmd[3] = start_addr & 0xff;
		trx( obj, cmd, 4, "\x0d", 1 );
		// high byte of start address
		cmd[0] = 0x29;
		cmd[1] = 0xc7;
		cmd[2] = 0x01;		// length
		cmd[3] = (start_addr >> 8)&0xff;
		trx( obj, cmd, 4, "\x0d", 1 );
		
		// setup write command
		cmd[0] = 0x29;
		cmd[1] = 0x84;
		cmd[2] = 0x01;	// len, only use 1
		for(i=0; i<len; i++)
		{
			cmd[3] = buf[i];
			if( !trx( obj, cmd, 4, "\x0d", 1 ) )
				return FALSE;	// failure
		}
	}	// End C2
	DUMP_FUNC_END();
	return TRUE;
}

/** this function performs the preamble and postamble
*/
BOOL ec2_write_xdata_page( EC2DRV *obj, char *buf, unsigned char page,
						   unsigned char start, int len )
{
	DUMP_FUNC();
	int i;
	char cmd[5];
	
	if( strcmp(obj->dev->name,"C8051F120")==0)
		trx(obj,"\x03\x02\x2E\x01",4,"\x0D",1);		// preamble
	else
		trx(obj,"\x03\x02\x2D\x01",4,"\x0D",1);		// preamble
	
	// select page
	cmd[0] = 0x03;
	cmd[1] = 0x02;
	//cmd[2] = 0x32;
	// TODO: why a different number between the F020 and F120?  is this some register?
	//		is this different for other processors?
	if( strcmp(obj->dev->name, "C8051F020")==0 )
		cmd[2] = 0x32;	// F020 Value
	else
		cmd[2] = 0x31;	// F120 value
	cmd[3] = page;
	trx( obj, (char*)cmd, 4, "\x0D", 1 );
	
	// write bytes to page
	// up to 2 at a time
	for( i=0; i<len; i+=2 )
	{
		if( (len-i) > 1 )
		{
			cmd[0] = 0x07;
			cmd[1] = i+start;
			cmd[2] = 2;
			cmd[3] = (char)buf[i];
			cmd[4] = (char)buf[i+1];
			trx( obj, (char*)cmd, 5, "\x0d", 1 );
		}
		else
		{
			// single byte write
			// although the EC2 responds correctly to 1 byte writes the SI labs
			// ide dosen't use them and attempting to use them does not cause a
			// write.  We fake a single byte write by reading in the byte that
			// will be overwitten and rewrite it 
			ec2_read_xdata( obj, &cmd[3], (page<<8)+i+start, 2 );
			cmd[0] = 0x07;
			cmd[1] = i+start;
			cmd[2] = 2;								// length
			cmd[3] = (char)buf[i];					// overwrite first byte
			trx( obj, (char*)cmd, 5, "\x0d", 1 );	// test
		}
	}
//	trx( obj, "\x03\x02\x2D\x00", 4, "\x0D", 1);	// close xdata write session
	trx( obj, "\x03\x02\x2E\x00", 4, "\x0D", 1);	// close xdata write session	2e for F120, 2d for F020
	return TRUE;
}


BOOL ec2_write_xdata_c2_emif( EC2DRV *obj, char *buf, int start_addr, int len )
{
	// Command format
	// data upto 3C bytes, last byte is in a second USB transmission with its own length byte
	// 3f 00 00 3c 5a
	// 3f LL HH NN
	// where
	//		LL = Low byte of address to start writing at.
	//		HH = High byte of address to start writing at.
	//		NN = Number of  bytes to write, max 3c for EC3, max 0C? for EC2	
	
	// read back result of 0x0d
	
	assert( obj->mode==C2 );
	assert( obj->dev->has_external_bus );	
	
	uint16_t block_len_max = obj->dbg_adaptor==EC2 ? 0x0C : 0x3C;
	uint16_t block_len;
	uint16_t addr = start_addr;
	uint16_t cnt = 0;
	char cmd[64];
	const char cmd_len = 4;
	BOOL ok=TRUE;
	while( cnt<len )
	{
		cmd[0] = 0x3f;	// Write EMIF
		cmd[1] = addr&0xff;
		cmd[2] = (addr&0xff00)>>8;
		block_len = (len-cnt)>block_len_max ? block_len_max : len-cnt;
		cmd[3] = block_len;
		memcpy( &cmd[4], buf+addr, block_len );

		if( block_len==0x3c )
		{
			// split write over 2 USB writes
			write_port( obj, cmd, 0x3f );
			write_port( obj, &cmd[cmd_len+0x3b], 1 );
			ok |= (read_port_ch( obj)=='\x0d');
		}
		else
		{
			write_port( obj, cmd, block_len + cmd_len );
			ok |= (read_port_ch( obj)=='\x0d');
		}
		addr += block_len;
		cnt += block_len;
	}
	return ok;
}


/** Read len bytes of data from the target
  * starting at start_addr into buf
  *
  * T 03 02 2D 01  R 0D	<br>
  * T 03 02 32 addrH	<br>
  * T 06 02 addrL len	<br>
  * where len <= 0x0C	<br>
  *
  * \param buf buffer to recieve data read from XDATA
  * \param start_addr address to begin reading from, 0x00 - 0xFFFF
  * \param len Number of bytes to read, 0x00 - 0xFFFF
  */
void ec2_read_xdata( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	if( obj->mode==JTAG )
	{
	
		int blen, page;
		char start_page	= ( start_addr >> 8 ) & 0xFF;
		char last_page	= ( (start_addr+len-1) >> 8 ) & 0xFF;
		unsigned int ofs=0;
		unsigned int pg_start_addr, pg_end_addr;	// start and end addresses within page
		
		assert( start_addr>=0 && start_addr<=0xFFFF && start_addr+len<=0x10000 );
		memset( buf, 0xff, len );
		for( page = start_page; page<=last_page; page++ )
		{
			pg_start_addr = (page==start_page) ? start_addr&0x00FF : 0x00;	
			pg_end_addr = (page==last_page) ? (start_addr+len-1)-(page<<8) : 0xff;
			blen = pg_end_addr - pg_start_addr + 1;	
	//		printf("page = 0x%02x, start = 0x%04x, end = 0x%04x, len = %i\n", page,pg_start_addr, pg_end_addr,blen);
			ec2_read_xdata_page( obj, buf+ofs, page, pg_start_addr, blen );
			ofs += blen;
		}
	}	// end JTAG
	else if( obj->mode==C2 && obj->dev->has_external_bus )
	{
		ec2_read_xdata_c2_emif( obj, buf, start_addr, len );
	}
	else if( obj->mode==C2 && !obj->dev->has_external_bus )
	{
		// T 29 ad 01 10			R 0d		.// low byte of address 10 ( last byte of cmd)
		// T 29 c7 01 01			R 0d		//  high byte of address 01 ( last byte of cmd)
		// T 28 84 01				R 00		// read next byte	( once for every byte to be read )
		// C2 dosen't seem to need any paging like jtag mode does
		char cmd[4];
		unsigned int i;
		// low byte of start address
		cmd[0] = 0x29;
		cmd[1] = 0xad;
		cmd[2] = 0x01;		// length
		cmd[3] = start_addr & 0xff;
		trx( obj, cmd, 4, "\x0d", 1 );
		// high byte of start address
		cmd[0] = 0x29;
		cmd[1] = 0xc7;
		cmd[2] = 0x01;		// length
		cmd[3] = (start_addr >> 8)&0xff;
		trx( obj, cmd, 4, "\x0d", 1 );
		
		// setup read command
		cmd[0] = 0x28;
		cmd[1] = 0x84;
		cmd[2] = 0x01;
		for(i=0; i<len; i++)
		{
			write_port( obj, cmd, 3 );
			buf[i] = read_port_ch( obj ); 
		}
	}	// End C2
}


/** Read from xdata memory on chips that have external memory interfaces and C2
  * \param buf buffer to recieve data read from XDATA
  * \param start_addr address to begin reading from, 0x00 - 0xFFFF
  * \param len Number of bytes to read, 0x00 - 0xFFFF
  */
void ec2_read_xdata_c2_emif( EC2DRV *obj, char *buf, int start_addr, int len )
{
	// Command format
	//	T 3e LL HH NN
	// where
	//		LL = Low byte of address to start reading from
	//		HH = High byte of address to start reading from
	//		NN = Number of  bytes to read, max 3c for EC3, max 0C? for EC2
	assert( obj->mode==C2 );
	assert( obj->dev->has_external_bus );
	uint16_t block_len_max = obj->dbg_adaptor==EC2 ? 0x0C : 0x3C;
	// read  blocks of upto max block  len
	uint16_t addr = start_addr;
	uint16_t cnt = 0;
	uint16_t block_len;
	char cmd[4];
	while( cnt < len )
	{
		// request the block
		cmd[0] = 0x3e;					// Read EMIF
		cmd[1] = addr & 0xff;			// Low byte
		cmd[2] = (addr & 0xff00) >> 8;	// High byte
		block_len = (len-cnt)>block_len_max ? block_len_max : len-cnt;
		cmd[3] = block_len;
		write_port( obj, cmd, 4 );
		read_port( obj, buf+cnt, block_len );
		addr += block_len;
		cnt += block_len;
	}
}



void ec2_read_xdata_page( EC2DRV *obj, char *buf, unsigned char page,
						  unsigned char start, int len )
{
	DUMP_FUNC();
	unsigned int i;
	unsigned char cmd[0x0C];

	memset( buf, 0xff, len );	
	assert( (start+len) <= 0x100 );		// must be in one page only
	
	if( strcmp(obj->dev->name,"C8051F020")==0 )
	{
		trx( obj, "\x03\x02\x2D\x01", 4, "\x0D", 1 );		// 2d for 020 2e for f120
	}
	else
	{
		trx( obj, "\x03\x02\x2E\x01", 4, "\x0D", 1 );		// 2d for 020 2e for f120
	}
	// select page
	cmd[0] = 0x03;
	cmd[1] = 0x02;
	
	if( strcmp(obj->dev->name,"C8051F020")==0 )
		cmd[2] = 0x32;	// 31 for F120, 32 for F020
	else
		cmd[2] = 0x31;	// 31 for F120, 32 for F020

	cmd[3] = page;
	trx( obj, (char*)cmd, 4, "\x0D", 1 );
	if( obj->dbg_adaptor==EC2 ) usleep(10000);		// give the ec2 a rest.
	cmd[0] = 0x06;
	cmd[1] = 0x02;
	// read the rest
	for( i=0; i<len; i+=0x0C )
	{
		cmd[2] = i & 0xFF;
		cmd[3] = (len-i)>=0x0C ? 0x0C : (len-i);
		write_port( obj, (char*)cmd, 4 );
		if( obj->dbg_adaptor==EC2 ) usleep(10000);	// seems necessary for stability
		read_port( obj, buf, cmd[3] );
		buf += cmd[3];
	}
}

/** Read from Flash memory (CODE memory)
  *
  * \param buf buffer to recieve data read from CODE memory
  * \param start_addr address to begin reading from, 0x00 - 0xFFFF, 0x10000 - 0x1007F = scratchpad
  * \param len Number of bytes to read, 0x00 - 0xFFFF
  * \returns TRUE on success, otherwise FALSE
  */
BOOL ec2_read_flash( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	unsigned char cmd[0x0C];
	unsigned char acmd[7];
	int addr, i;
	
	if( obj->mode==JTAG )
	{
		// Preamble
		trx( obj, "\x02\x02\xB6\x01", 4, "\x80", 1 );
		trx( obj, "\x02\x02\xB2\x01", 4, "\x14", 1 );
		trx( obj, "\x03\x02\xB2\x04", 4, "\x0D", 1 );
		trx( obj, "\x0B\x02\x04\x00", 4, "\x0D", 1 );
		trx( obj, "\x0D\x05\x85\x08\x01\x00\x00", 7, "\x0D", 1 );
		addr = start_addr;
		memcpy(acmd,"\x0D\x05\x84\x10\x00\x00\x00",7);
		acmd[4] = addr & 0xFF;						// Little endian
		acmd[5] = (addr>>8) & 0xFF;					// patch in actual address
		trx( obj, (char*)acmd, 7, "\x0D", 1 );		// first address write
	
		if( start_addr>=0x10000 && start_addr<=0x1007f )
		{
			// scratchpad mode
			start_addr -= 0x10000;
			// 82 flash control reg ( scratchpad access )
			trx( obj, "\x0D\x05\x82\x08\x81\x00\x00", 7, "\x0D", 1 );
		}
		else
		{
			// normal program memory
			// 82 flash control reg
			trx( obj, "\x0D\x05\x82\x08\x01\x00\x00", 7, "\x0D", 1 );
		}
	
		memset( buf, 0xff, len );
	
		for( i=0; i<len; i+=0x0C )
		{
			addr = start_addr + i;
			acmd[4] = addr & 0xFF;					// Little endian, flash address
			acmd[5] = (addr>>8) & 0xFF;				// patch in actual address
			trx( obj, (char*)acmd, 7, "\x0D", 1 );	// write address
			// read command
			// cmd 0x11 0x02 <len> 00
			// where len <= 0xC0
			cmd[0] = 0x11;
			cmd[1] = 0x02;
			cmd[2] = (len-i)>=0x0C ? 0x0C : (len-i);
			cmd[3] = 0x00;
			write_port( obj, (char*)cmd, 4 );
			read_port( obj, buf+i, cmd[2] ); 
		}
	
		trx( obj, "\x0D\x05\x82\x08\x00\x00\x00", 7, "\x0D", 1 );
		trx( obj, "\x0B\x02\x01\x00", 4, "\x0D", 1 );
		trx( obj, "\x03\x02\xB6\x80", 4, "\x0D", 1 );
		trx( obj, "\x03\x02\xB2\x14", 4, "\x0D", 1 );
	} else if( obj->mode==C2 )
	{
		// C2 mode is much simpler
		//
		// example command 0x2E 0x00 0x00 0x0C
		//				     |    |   |    |
		//					 |    |   |    +---- Length or read up to 0x0C bytes
		//					 |    |   +--------- High byte of address to start at
		//					 |    +----(len-i)--------- Low byte of address
		//					 +------------------ Flash read command
		cmd[0] = 0x2E;
		for( i=0; i<len; i+=0x0c )
		{
			addr = start_addr + i;
			cmd[1] = addr & 0xff;	// low byte
			cmd[2] = (addr >> 8) & 0xff;
			cmd[3] = (len-i) > 0x0c ? 0x0c : (len-i);
			write_port( obj, (char*)cmd, 4 );
			read_port( obj, buf+i, cmd[3] ); 
		}
	}
	return TRUE;
}


/** Set flash address register, Internal helper function, 
  * Note that flash preamble must be used before this can be used successfully
  */
static void set_flash_addr_jtag( EC2DRV *obj, int16_t addr )
{
	DUMP_FUNC();
	char cmd[7] = "\x0D\x05\x84\x10\x00\x00\x00";
	cmd[4] = addr & 0xFF;
	cmd[5] = (addr >> 8) & 0xFF;
	trx( obj, cmd, 7, "\x0D", 1 );
}

/** Write to flash memory
  * This function assumes the specified area of flash is already erased
  * to 0xFF before it is called.
  *
  * Writes to a location that already contains data will only be successfull
  * in changing 1's to 0's.
  *
  * \param buf buffer containing data to write to CODE
  * \param start_addr address to begin writing at, 0x00 - 0xFFFF
  * \param len Number of bytes to write, 0x00 - 0xFFFF
  *
  * \returns TRUE on success, otherwise FALSE
  */
BOOL ec2_write_flash( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	if( obj->mode==C2 )
		return ec2_write_flash_c2( obj, buf, start_addr, len );
	else
		return ec2_write_flash_jtag( obj, buf, start_addr, len );
}

BOOL ec2_write_flash_jtag( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	int first_sector = start_addr>>9;
	int end_addr = start_addr + len - 1;
	int last_sector = end_addr>>9;
	int sector_cnt = last_sector - first_sector + 1;
	uint16_t addr, sec_end_addr, i;
	char cmd[16];
	//printf("ec2_write_flash( char *buf, 0x%04x, 0x%04x\n", (unsigned int)start_addr, (unsigned int) len );
	//printf("first=0x%04x, last = 0x%04x\n",(unsigned int)first_sector,(unsigned int)last_sector);
	
	// flash access preamble
	trx( obj, "\x02\x02\xB6\x01", 4, "\x80", 1 );
	trx( obj, "\x02\x02\xB2\x01", 4, "\x14", 1 );
	trx( obj, "\x03\x02\xB2\x04", 4, "\x0D", 1 );
	trx( obj, "\x0B\x02\x04\x00", 4, "\x0D", 1 );

	// is first write on a sector boundary?...  it dosen't matter
	addr = start_addr;

	for( i=0; i<sector_cnt; i++)
	{
//		printf("sector number %i/%i, addr = 0x%04x\n",i,sector_cnt,(unsigned int)addr );
		// page preamble for each page
		trx( obj, "\x0d\x05\x85\x08\x01\x00\x00", 7, "\x0d", 1 );
		trx( obj, "\x0d\x05\x82\x08\x20\x00\x00", 7, "\x0d", 1 );
		set_flash_addr_jtag( obj, addr );
		trx( obj, "\x0f\x01\xa5", 3, "\x0d", 1 );
		trx( obj, "\x0d\x05\x82\x08\x02\x00\x00", 7, "\x0d", 1 );
		trx( obj, "\x0e\x00", 2, "\xa5", 1 );	// ???
		trx( obj, "\x0e\x00", 2, "\xff", 1 );	// ???
		trx( obj, "\x0d\x05\x82\x08\x10\x00\x00",7,"\x0d",1);
		set_flash_addr_jtag( obj, addr );
		sec_end_addr = (first_sector<<9) + (i+1)*0x200;
//		printf("sector number %i/%i, start addr = 0x%04x, end_addr = 0x%04x\n",
//				i,sector_cnt,(unsigned int)addr,(unsigned int)sec_end_addr );
		if( i == (sector_cnt-1) )
		{
//			printf("Last sector\n");
			sec_end_addr = start_addr + len;
		}
		// write all bytes this page
		cmd[0] = 0x12;
		cmd[1] = 0x02;
		cmd[2] = 0x0C;
		cmd[3] = 0x00;
		while( sec_end_addr-addr > 0x0c )		// @FIXME: need to take into account actual length
		{
			memcpy( &cmd[4], buf, 0x0c );
			trx( obj, cmd, 16, "\x0d", 1 );
			addr += 0x0c;
			buf += 0x0c;
		}
		// mop up whats left
//		printf("addr = 0x%04x, overhang = %i\n",(unsigned int)addr,(sec_end_addr-addr));
		cmd[2] = sec_end_addr-addr;
		if( cmd[2]>0 )
		{
			memcpy( &cmd[4], buf, cmd[2] );
			buf += cmd[2];
			addr += cmd[2];
			trx( obj, cmd, cmd[2]+4, "\x0d", 1 );
		}
	}
	// postamble
	trx( obj, "\x0d\x05\x82\x08\x00\x00\x00", 7, "\x0d", 1 );
	trx( obj, "\x0b\x02\x01\x00", 4, "\x0d", 1 );
	trx( obj, "\x03\x02\xB6\x80", 4, "\x0d", 1 );
	trx( obj, "\x03\x02\xB2\x14", 4, "\x0d", 1 );
	return TRUE;
}

/*  C2 version of ec2_write_flash
*/
BOOL ec2_write_flash_c2( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	// preamble
	// ...
	// 2f connect breakdown:
	// T 2f 00 30 08 55 55 55 55 55 55 55 55		R 0d
	//    |  |  |  | +---+--+--+--+--+--+--+--- Data bytes to write
	//    |  |  |  +--------------------------- number of data bytes towrite (8 max, maxtotal cmd length 0x0c)
	//    |  |  +------------------------------ High byte of address to start write
	//    |  +--------------------------------- low byte of address
	//    +------------------------------------ write code memory command
	//
	// for some funny reason the IDE alternates between 8 byte writes and 
	// 4 byte writes, this means the total number of writes cycle is 0x0c the
	// exact same number as the JTAG mode, it looks like they economise code,
	// I this would complicate things, well just do 8 byte writes and then an
	// fragment at the end.  This will need testing throughlerly as it is
	// different to the IDE's action.
	unsigned int i, addr;
	char		 cmd[0x0c];

	cmd[0] = 0x2f;									// Write code/flash memory cmd	
	for( i=0; i<len; i+=8 )
	{
		addr = start_addr + i;
		cmd[1] = addr & 0xff;						// low byte
		cmd[2] = (addr>>8) & 0xff;					// high byte
		cmd[3] = (len-i)<8 ? (len-i) : 8;
		memcpy( &cmd[4], &buf[i], cmd[3] );
		if( !trx( obj, cmd, cmd[3]+4, "\x0d", 1 ) )
			return FALSE;							// Failure
	}
	return TRUE;
}


/** This variant of writing to flash memory (CODE space) will erase sectors
  * before writing.
  *
  * \param buf buffer containing data to write to CODE
  * \param start_addr address to begin writing at, 0x00 - 0xFFFF
  * \param len Number of bytes to write, 0x00 - 0xFFFF
  *
  * \returns TRUE on success, otherwise FALSE
  */
BOOL ec2_write_flash_auto_erase( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	int first_sector = start_addr>>9;		// 512 byte sectors
	int end_addr = start_addr + len - 1;
	int last_sector = end_addr>>9;
	int sector_cnt = last_sector - first_sector + 1;
	int i;

	// Erase sectors involved
	for( i=0; i<sector_cnt; i++ )
		ec2_erase_flash_sector( obj, (first_sector + i)<<9  );
	ec2_write_flash( obj, buf, start_addr, len );	// why is this broken?
	
	return TRUE;	///< @TODO check to successful erase
}

/** This variant of writing to flash memory (CODE space) will read all sector
  * content before erasing and will merge changes over the existing data
  * before writing.
  * This is slower than the other methods in that it requires a read of the
  * sector first.  also blank sectors will not be erased again
  *
  * \param buf buffer containing data to write to CODE
  * \param start_addr address to begin writing at, 0x00 - 0xFFFF
  * \param len Number of bytes to write, 0x00 - 0xFFFF
  *
  * \returns TRUE on success, otherwise FALSE
  */
BOOL ec2_write_flash_auto_keep( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	int first_sector = start_addr>>9;		// 512 byte sectors
	int first_sec_addr = first_sector<<9;	// 512 byte sectors
	int end_addr = start_addr + len - 1;
	int last_sector = end_addr>>9;
	int sector_cnt = last_sector - first_sector + 1;
	int i,j;
	char tbuf[0x10000];

	// read in all sectors that are affected
	ec2_read_flash( obj, tbuf, first_sec_addr, sector_cnt*0x200 );

	// erase nonblank sectors
	for( i=0; i<sector_cnt; i++)
	{
		j = 0;
		while( j<0x200 )
		{
			if( (unsigned char)tbuf[i*0x200+j] != 0xFF )
			{
				// not blank, erase it
				ec2_erase_flash_sector( obj, first_sec_addr + i * 0x200 );
				break;
			}
			j++;
		}
	}

	// merge data then write
	memcpy( tbuf + ( start_addr - first_sec_addr ), buf, len );
	return ec2_write_flash( obj, tbuf, first_sec_addr, sector_cnt*0x200 );
}


/** Erase all CODE memory flash in the device
  */
void ec2_erase_flash( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->mode==C2 )
	{
		int i;
		// generic C2 erase entire device
		// works for EC2 and EC3
		
		// FIXME the disconnect / connect sequence dosen't work with the EC2 and C2 mode!
		if( obj->dbg_adaptor=EC3 )
		{
			ec2_disconnect( obj );
			ec2_connect( obj, obj->port );
		}
		write_port( obj, "\x3C",4);			// Erase entire device
		if( obj->dbg_adaptor=EC3 )
		{
			ec2_disconnect( obj );
			ec2_connect( obj, obj->port );
		}
	}
	else if( obj->mode==JTAG )
	{
		ec2_disconnect( obj );
		ec2_connect( obj, obj->port );
		trx( obj,"\x0B\x02\x04\x00",4,"\x0D",1);	// CPU core suspend
		trx( obj,"\x0D\x05\x85\x08\x00\x00\x00",7,"\x0D",1);
		trx( obj,"\x0D\x05\x82\x08\x20\x00\x00",7,"\x0D",1);
		
		// we do need the following lines because some processor families like the F04x have
		// both 64K and 32K variants and no distinguishing device id,, just a whole family id
		if( obj->dev->lock_type==FLT_RW_ALT )
			set_flash_addr_jtag( obj, obj->dev->lock );	// alternate lock byte families

		if( obj->dev->lock_type==FLT_RW || obj->dev->lock_type==FLT_RW_ALT )
		{
			set_flash_addr_jtag( obj, obj->dev->read_lock );
		}
		else
			set_flash_addr_jtag( obj, obj->dev->lock );
		trx( obj,"\x0F\x01\xA5",3,"\x0D",1);		// erase sector
		ec2_disconnect( obj );
		ec2_connect( obj, obj->port );
	}
}

/** Erase a single sector of flash memory
  * \param sect_addr base address of sector to erase.  
  * 				Does not necessarilly have to be the base addreres but any
  *					address within the sector is equally valid
  *
  */
void ec2_erase_flash_sector( EC2DRV *obj, int sect_addr )
{
	DUMP_FUNC();
	if( obj->mode == JTAG )
	{
		assert( sect_addr>=0 && sect_addr<=0xFFFF );
		sect_addr &= 0xFE00;								// 512 byte sectors
	//	printf("Erasing sector at 0x%04x ... ",sect_addr);	
	
		trx( obj, "\x02\x02\xB6\x01", 4, "\x80", 1 );
		trx( obj, "\x02\x02\xB2\x01", 4, "\x14", 1 );
		trx( obj, "\x03\x02\xB2\x04", 4, "\x0D", 1 );
		trx( obj, "\x0B\x02\x04\x00", 4, "\x0D", 1 );
		trx( obj, "\x0D\x05\x82\x08\x20\x00\x00", 7, "\x0D", 1 );
		set_flash_addr_jtag(  obj, sect_addr );
	
		trx( obj, "\x0F\x01\xA5", 3, "\x0D", 1 );
		
		// cleanup
		trx( obj, "\x0B\x02\x01\x00", 4, "\x0D", 1 );
		trx( obj, "\x03\x02\xB6\x80", 4, "\x0D", 1 );
		trx( obj, "\x03\x02\xB2\x14", 4, "\x0D", 1 );
	}	// end JTAG
	else if( obj->mode == C2 )
	{
		/// \TODO confirm this is actually the erase command and nothing else in necessary
		char cmd[2];
		cmd[0] = 0x30;			// sector erase command
		cmd[1] = sect_addr>>9;	// sector number (512 byte sectors)
		trx( obj, cmd, 2, "\x0d", 1 );
	}	// End C2
}

/** Read from the scratchpad area in flash.
	Address range 0x00 - 0x7F
*/
BOOL ec2_read_flash_scratchpad( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	return ec2_read_flash( obj, buf, start_addr + 0x10000, len );
}

/** Write to the scratchpad page of flash.
	The locations being modified must have been erased first of be 
	having their values burn't down.
	
	\param buf			buffer containing data to write.
	\param start_addr	Address to begin writing at 0x00 - 0x7f.
	\param len			number of byte to write
	\returns			TRUE on success, FALSE otherwise
*/
BOOL ec2_write_flash_scratchpad( EC2DRV *obj, char *buf, int start_addr, int len )
{
	DUMP_FUNC();
	int i;
	char cmd[0x10];
	
	update_progress( obj, 0 );
	// preamble
	trx( obj, "\x02\x02\xb6\x01", 4, "\x80", 1 );
	trx( obj, "\x02\x02\xb2\x01", 4, "\x14", 1 );
	trx( obj, "\x03\x02\xb2\x04", 4, "\x0d", 1 );
	trx( obj, "\x0b\x02\x04\x00", 4, "\x0d", 1 );

	trx( obj, "\x0d\x05\x82\x08\x90\x00\x00", 7, "\x0d", 1 );
	set_flash_addr_jtag( obj, start_addr );	
	cmd[0] = 0x12;
	cmd[1] = 0x02;
	// cmd[2] = length of block being written (max 0x0c)
	cmd[3] = 0x00;
	for( i=0; i<len; i+= 0x0c )
	{
		cmd[2] = (len-i)>0x0c ? 0x0c : len-i;
		memcpy( &cmd[4], &buf[i], cmd[2] );
		write_port( obj, cmd, 4 + cmd[2] );
		if( read_port_ch( obj )!='\x0d' )
			return FALSE;
		update_progress( obj, i*100/len );
	}
	
	// cleanup
	trx( obj, "\x0b\x02\x01\x00", 4, "\x0d", 1 );
	trx( obj, "\x03\x02\xb6\x80", 4, "\x0d", 1 );
	trx( obj, "\x03\x02\xb2\x14", 4, "\x0d", 1 );
	return TRUE;
}

void ec2_write_flash_scratchpad_merge( EC2DRV *obj, char *buf,
                                       int start_addr, int len )
{
	DUMP_FUNC();
	char mbuf[0x80];
	/// @todo	add erase only when necessary checks
	update_progress( obj, 0 );
	ec2_read_flash_scratchpad( obj, mbuf, 0, 0x80 );
	memcpy( &mbuf[start_addr], buf, len );	// merge in changes
	update_progress( obj, 45 );
	ec2_erase_flash_scratchpad( obj );
	update_progress( obj, 55 );
	ec2_write_flash_scratchpad( obj, mbuf, 0, 0x80 );
	update_progress( obj, 100 );
}

void ec2_erase_flash_scratchpad( EC2DRV *obj )
{
	DUMP_FUNC();
	// preamble
	trx( obj, "\x02\x02\xB6\x01", 4, "\x80", 1 );
	trx( obj, "\x02\x02\xB2\x01", 4, "\x14", 1 );
	trx( obj, "\x03\x02\xB2\x04", 4, "\x0D", 1 );
	trx( obj, "\x0B\x02\x04\x00", 4, "\x0D", 1 );
	
	// erase scratchpad
	trx( obj, "\x0D\x05\x82\x08\xA0\x00\x00", 7, "\x0D", 1 );
	trx( obj, "\x0D\x05\x84\x10\x00\x00\x00", 7, "\x0D", 1 );
	trx( obj, "\x0F\x01\xA5", 3, "\x0D", 1 );
	
	// cleanup
	trx( obj, "\x0B\x02\x01\x00", 4, "\x0D", 1 );
	trx( obj, "\x03\x02\xB6\x80", 4, "\x0D", 1 );
	trx( obj, "\x03\x02\xB2\x14", 4, "\x0D", 1 );
}


/** read the currently active set of R0-R7
  * the first returned value is R0
  * \note This needs more testing, seems to corrupt R0
  * \param buf buffer to reciere results, must be at least 8 bytes only
  */
void read_active_regs( EC2DRV *obj, char *buf )
{
	DUMP_FUNC();
	char psw;
	int addr;
	// read PSW
	ec2_read_sfr( obj, &psw, 0xD0 );
	printf( "PSW = 0x%02x\n",psw );

	// determine correct address
	addr = ((psw&0x18)>>3) * 8;
	printf("address = 0x%02x\n",addr);
	ec2_read_ram( obj, buf, addr, 8 );

	// R0-R1
	write_port( obj, "\x02\x02\x24\x02", 4 );
	read_port( obj, &buf[0], 2 );
}

/** Read the targets program counter
  *
  * \returns current address of program counter (16-bits)
  */
uint16_t ec2_read_pc( EC2DRV *obj )
{
	DUMP_FUNC();
	unsigned char buf[2];

	if( obj->mode==JTAG )
	{
		write_port( obj, "\x02\x02\x20\x02", 4 );
		read_port(  obj, (char*)buf, 2 );
	}
	else if( obj->mode==C2 )
	{
		write_port( obj, "\x28\x20\x02", 3 );
		read_port(  obj, (char*)buf, 2 );
	}
	return ((buf[1]<<8) | buf[0]);
}

void ec2_set_pc( EC2DRV *obj, uint16_t addr )
{
	DUMP_FUNC();
	char cmd[4];
	if( obj->mode==JTAG )
	{
		cmd[0] = 0x03;
		cmd[1] = 0x02;
		cmd[2] = 0x20;
		cmd[3] = addr&0xFF;
		trx( obj, cmd, 4, "\x0D", 1 );
		cmd[2] = 0x21;
		cmd[3] = (addr>>8)&0xFF;
		trx( obj, cmd, 4, "\x0D", 1 );
	}
	else if( obj->mode==C2 )
	{
		cmd[0] = 0x29;
		cmd[1] = 0x20;
		cmd[2] = 0x01;					// len
		cmd[3] = addr & 0xff;			// low byte addr
		trx( obj, cmd, 4,"\x0d", 1 );
		cmd[1] = 0x21;
		cmd[3] = addr>>8;				// high byte
		trx( obj, cmd, 4, "\x0d", 1 );
	}
}


/** Cause the processor to step forward one instruction
  * The program counter must be setup to point to valid code before this is
  * called. Once that is done this function can be called repeatedly to step
  * through code.
  * It is likely that in most cases the debugger will request register dumps
  * etc between each step but this function provides just the raw step
  * interface.
  * 
  * \returns instruction address after the step operation
  */
uint16_t ec2_step( EC2DRV *obj )
{
	DUMP_FUNC();
	char buf[2];
	
	if( obj->mode==JTAG )
	{
		trx( obj, "\x09\x00", 2, "\x0d", 1 );
		trx( obj, "\x13\x00", 2, "\x01", 1 );		// very similar to 1/2 a target_halt command,  test to see if stopped...
		
		write_port( obj, "\x02\x02\x20\x02", 4 );
		read_port(  obj, buf, 2 );
		return buf[0] | (buf[1]<<8);
	}
	else if( obj->mode==C2 )
	{
		trx( obj, "\x26", 1, "\x0d", 1 );
		return ec2_read_pc(obj);
	}
	return 0;	// Invalid mode
}

/** Start the target processor running from the current PC location
  *
  * \returns TRUE on success, FALSE otherwise
  */
BOOL ec2_target_go( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->mode==JTAG )
	{
		if( !trx( obj, "\x0b\x02\x00\x00", 4, "\x0d", 1 ) )
			return FALSE;
		if( !trx( obj, "\x09\x00", 2, "\x0d", 1 ) )
			return FALSE;
	}
	else if( obj->mode==C2 )
	{
		if( !trx( obj, "\x24", 1, "\x0d", 1 ) )
			return FALSE;
		if( !trx( obj, "\x27", 1, "\x00", 1 ) )		// indicates running
			return FALSE;
	}
	else
		return FALSE;
	return TRUE;
}

/** Poll the target to determine if the processor has halted.
  * The halt may be caused by a breakpoint of the ec2_target_halt() command.
  *
  * For run to breakpoint it is necessary to call this function regularly to
  * determine when the processor has actually come accross a breakpoint and
  * stopped.
  *
  * Recommended polling rate every 250ms.
  *
  * \returns TRUE if processor has halted, FALSE otherwise
  */
BOOL ec2_target_halt_poll( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->mode==JTAG )
		write_port( obj, "\x13\x00", 2 );
	else if( obj->mode==C2 )
		write_port( obj, "\x27", 1 );
		//write_port( obj, "\x27\x00", 2 );
	return read_port_ch( obj )==0x01;	// 01h = stopped, 00h still running
}

/** Cause target to run until the next breakpoint is hit.
  * \note this function will not return until a breakpoint it hit.
  * 
  * \returns Adderess of breakpoint at which the target stopped
  */
uint16_t ec2_target_run_bp( EC2DRV *obj, BOOL *bRunning )
{
	DUMP_FUNC();
	int i;
	ec2_target_go( obj );
	if( obj->dbg_adaptor )		// @FIXME: which debug adapter?
	{
		trx( obj, "\x0C\x02\xA0\x10", 4, "\x00\x01\x00", 3 );
		trx( obj, "\x0C\x02\xA1\x10", 4, "\x00\x00\x00", 3 );
		trx( obj, "\x0C\x02\xB0\x09", 4, "\x00\x00\x01", 3 );
		trx( obj, "\x0C\x02\xB1\x09", 4, "\x00\x00\x01", 3 );
		trx( obj, "\x0C\x02\xB2\x0B", 4," \x00\x00\x20", 3 );
	}
	
	// dump current breakpoints for debugging
	for( i=0; i<4;i++)
	{
		if( getBP( obj, obj->bpaddr[i] )!=-1 )
			printf("bpaddr[%i] = 0x%04x\n",i,(unsigned int)obj->bpaddr[i]);
	}
	
	while( !ec2_target_halt_poll( obj )&&(*bRunning) )
		usleep(250);
	return ec2_read_pc( obj );
}

/** Request the target processor to stop
  * the polling is necessary to determine when it has actually stopped
  */
BOOL ec2_target_halt( EC2DRV *obj )
{
	DUMP_FUNC();
	int i;

	if( obj->mode==JTAG )
	{
//		trx( obj, "\x0B\x02\x02\x00",4,"\x0D",1);	// system reset??? is this the right place.  won''t this break debugging modes (run/stop since a reset is bad. test
		// the above should only occur when halt is used as part of an init sequence.
		if( !trx( obj, "\x0B\x02\x01\x00", 4, "\x0d", 1 ) )
			return FALSE;
	}
	else if( obj->mode==C2 )
	{
		if( !trx( obj, "\x25", 1, "\x0d", 1 ) )
			return FALSE;
	}
	else 
		return FALSE;
	
	// loop allows upto 8 retries 
	// returns 0x01 of successful stop, 0x00 otherwise suchas already stopped	
	for( i=0; i<8; i++ )
	{
		if( ec2_target_halt_poll( obj ) )
			return TRUE;	// success
	}
	printf("ERROR: target would not stop after halt!\n");
	return FALSE;
}


/** Rest the target processor
  * This reset is a cut down form of the one used by the IDE which seems to 
  * read 2 64byte blocks from flash as well.
  * \todo investigate if the additional reads are necessary
  */
BOOL ec2_target_reset( EC2DRV *obj )
{
	DUMP_FUNC();
	BOOL r = TRUE;

	if( obj->mode == JTAG )
	{
		r &= trx( obj, "\x04", 1, "\x0D", 1 );
		r &= trx( obj, "\x1A\x06\x00\x00\x00\x00\x00\x00", 8, "\x0D", 1 );
		r &= trx( obj, "\x0B\x02\x02\x00", 4, "\x0D", 1 );	// sys reset
		r &= trx( obj, "\x14\x02\x10\x00", 4, "\x04", 1 );
		r &= trx( obj, "\x16\x02\x01\x20", 4, "\x01\x00", 2 );
		r &= trx( obj, "\x14\x02\x10\x00", 4, "\x04", 1 );
		r &= trx( obj, "\x16\x02\x81\x20", 4, "\x01\x00", 2 );
		r &= trx( obj, "\x14\x02\x10\x00", 4, "\x04", 1 );
		r &= trx( obj, "\x16\x02\x81\x30", 4, "\x01\x00", 2 );
		r &= trx( obj, "\x15\x02\x08\x00", 4, "\x04", 1 );
		r &= trx( obj, "\x16\x01\xE0", 3, "\x00", 1 );
		
		r &= trx( obj, "\x0B\x02\x01\x00", 4,"\x0D", 1 );
		r &= trx( obj, "\x13\x00", 2, "\x01", 1 );
		r &= trx( obj, "\x03\x02\x00\x00", 4, "\x0D", 1 );
	}
	else if( obj->mode==C2 )
	{
#if 0
			printf("running expirimental code!!!!!!\n");
			printf("-------- init begin -----------\n");
			// expirimental one for F340
			// hard coded for full access as a simple means to get things working before
			// figuring out the minimum set required
			r &= trx( obj, "\x20",1,"\x0D",1);			// select C2 mode
			r &= trx( obj, "\x22",1,"\x0f\x02\x0d",3);	// dev id = 0x0F(F340), rev = 2
			r &= trx( obj, "\x23",1,"\x08\x7d\x0d",3);
			r &= trx( obj, "\x36\xff\x01",3,"\xd8\x0d",2);	// read ?ff? and save in temp store 0xd8
			r &= trx( obj, "\x36\xbf\x01",3,"\x01\x0d",2);	// read ?bf? and save in temp store 0x01
			r &= trx( obj, "\x37\xbf\x01\x01",4,"\x0d",2);	// write ?bf? with 0x01
			r &= trx( obj, "\x36\xa0\x01",3,"\x80\x0d",2);	// read ?a0? and save in temp store 0x80
			r &= trx( obj, "\x37\xa0\x01\x90",4,"\x0d",1);	// write ?a0? with 0x90
			r &= trx( obj, "\x28\xbf\x01",3,"\x00\x0d",2);	// read SFR(FLKEY) = 0x00
			r &= trx( obj, "\x28\xef\x01",3,"\x4a\x0d",2);	// read SFR(EIE2) = 0x4a
			r &= trx( obj, "\x37\xa0\x01\x80",4,"\x0d",1);	// write 0x80 to ?a0?
			r &= trx( obj, "\x37\xbf\x01\x01",4,"\x0d",1);	// write 0x01 to ?bf?
			r &= trx( obj, "\x2e\x00\x00\x01",4,"\x02\x0d",2);	// read a byte of code at 0x0000
			
			r &= trx( obj, "\x36\xff\x01",3,"\xd8\x0d",2);	// read ?ff?  0xd8
			r &= trx( obj, "\x36\xbf\x01",3,"\x01\x0d",2);	// read ?bf?  0x01
			r &= trx( obj, "\x37\xbf\x01\x01",4,"\x0d",1);	// write 0x01 to ?bf?
			r &= trx( obj, "\x36\xa0\x01",3,"\x80\x0d",2);	// read ?a0? and save in temp store 0x80
			r &= trx( obj, "\x37\xa0\x01\x90",4,"\x0d",1);	// write 0x90 to ?a0?
			r &= trx( obj, "\x28\xbf\x01",3,"\x00\x0d",2);	// read SFR(FLKEY) = 0x00
			r &= trx( obj, "\x28\xef\x01",3,"\x4a\x0d",2);	// read SFR(EIE2) = 0x4a
			r &= trx( obj, "\x37\xa0\x01\x80",4,"\x0d",1);	// write 0x80 to ?a0?
			r &= trx( obj, "\x37\xbf\x01\x01",4,"\x0d",1);	// write 0x01 to ?bf?
			r &= trx( obj, "\x2e\xff\xfb\x01",4,"\xff\x0d",2);	// read 1 byte of code at 0xfbff	(lock byte)
			
			r &= trx( obj, "\x28\x20\x02",3,"\x00\x00\x0d",3);	// read SFR(20) NOT a real SFR its related to R0 / ram loc 0
			r &= trx( obj, "\x2a\x00\x03",3,"\x03\x01\xfb\x0d",4);
			r &= trx( obj, "\x28\x24\x02",3,"\x00\x00\x0d",3);	// special read R0/r/2???
			r &= trx( obj, "\x28\x26\x02",3,"\x00\x00\x0d",3);	// special read R0/r/2???
			
			r &= trx( obj, "\x36\xff\x01",3,"\xd8\x0d",2);		// read ?ff?  0xd8
			r &= trx( obj, "\x36\xbf\x01",3,"\x01\x0d",2);		// read ?bf?  0x01
			r &= trx( obj, "\x37\xbf\x01\x01",4,"\x0d",1);		// write 0x01 to ?bf?
			r &= trx( obj, "\x36\xa0\x01",3,"\x80\x0d",2);		// read ?a0? and save in temp store 0x80
			r &= trx( obj, "\x37\xa0\x01\x90",4,"\x0d",1);		// write 0x90 to ?a0?
			r &= trx( obj, "\x28\xbf\x01",3,"\x00\x0d",2);		// read SFR(FLKEY) = 0x00
			r &= trx( obj, "\x28\xef\x01",3,"\x4a\x0d",2);		// read SFR(EIE2) = 0x4a
			r &= trx( obj, "\x37\xa0\x01\x80",4,"\x0d",1);		// write 0x80 to ?a0?
			r &= trx( obj, "\x37\xbf\x01\x01",4,"\x0d",1);		// write 0x01 to ?bf?

			write_port( obj,"\x2e\x00\x00\x3c",4);				// Read code memory starting at 0x0000, 0x3c bytes
			uint8_t buf[255];
			read_port( obj, buf, 0x3d );		// result of read
			printf("First 0x3d bytes of flash followed by 0x3d\n");
			print_buf( buf, 0x3d );
			printf("----- end init -------\n");
			
			// tested for F310
//			r &= trx( obj, "\x20",1,"\x0D",1);
//			r &= trx( obj, "\x22",1,"\x08\x01",2);
//			r &= trx( obj, "\x23",1,"\x07\x50",2);
//	//		r &= trx( obj, "\x2E\x00\x00\x01",4,"\x02\x0D",2);
//			r &= trx( obj, "\x2E\xFF\x3D\x01",4,"\xFF",1);
//			char buf[32];
//			ec2_read_flash( obj, buf, 0x0000, 1 );
//			ec2_read_flash( obj, buf, 0x3dff, 1 );	// flash lock byte

#else
/*	Dosen't look like this is needed
		r &= trx( obj, "\x2a\x00\x03\x20", 4, "\x0d", 1 );
		r &= trx( obj, "\x29\x24\x01\x00", 4, "\x0d", 1 );
		r &= trx( obj, "\x29\x25\x01\x00", 4, "\x0d", 1 );
		r &= trx( obj, "\x29\x26\x01\x3d", 4, "\x0d", 1 );
		r &= trx( obj, "\x28\x20\x02", 3, "\x00\x00", 2 );
		r &= trx( obj, "\x2a\x00\x03", 3, "\x03\x01\x00", 3 );
		r &= trx( obj, "\x28\x24\x02", 3, "\x00\x00", 2 );
		r &= trx( obj, "\x28\x26\x02", 3, "\x3d\x00", 2 );
*/
	//hmm C2 device reset seems wrong.
		// new expirimental code
//		r &= trx( obj, "\x2E\x00\x00\x01",4,"\x02\x0D",2);
//		r &= trx( obj, "\x2E\xFF\x3D\x01",4,"\xFF",1);
#endif
	}
	return r;
}

/** Read the lock byte on single lock devices such as the F310.
	\returns read lock byte of devices with 1 lock byte
*/
uint8_t flash_lock_byte( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->dev->lock_type==FLT_SINGLE || obj->dev->lock_type==FLT_SINGLE_ALT)
	{
		return 0;	/// @TODO implement
	}
	else
		return 0;	// oops device dosen't have a single lock byte
}

/** Read the flash read lock byte
	\returns read lock byte of devices with 2 lock bytes
*/
uint8_t flash_read_lock( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->dev->lock_type==FLT_RW || obj->dev->lock_type==FLT_RW_ALT )
	{
		return 0;	/// @TODO implement
	}
	return 0;
}

/** Read the flash write/erase lock
	\returns the write/erase lock byte
*/
uint8_t flash_write_erase_lock( EC2DRV *obj )
{
	DUMP_FUNC();
	if( obj->dev->lock_type==FLT_RW || obj->dev->lock_type==FLT_RW_ALT )
	{
		/// @TODO implement
		
	}
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Breakpoint support                                                        //
///////////////////////////////////////////////////////////////////////////////

void dump_bp( EC2DRV *obj )
{
	DUMP_FUNC();
	int bp;
	printf("BP Dump:\n");
	for( bp=0; bp<4; bp++ )
	{
		printf(	"\t%i\t0x%04x\t%s\n",
				bp,obj->bpaddr[bp],
				((obj->bp_flags>>bp)&0x01) ? "Active" : "inactive" );
	}
}

/** Clear all breakpoints in the local table and also in the hardware.
*/
void ec2_clear_all_bp( EC2DRV *obj )
{
	DUMP_FUNC();
	int bp;
	for( bp=0; bp<4; bp++ )
		setBpMask( obj, bp, FALSE );
	if(obj->debug)
		dump_bp(obj);
}

/** Determine if there is a free breakpoint and then returning its index
  * \returns the next available breakpoint index, -1 on failure
 */
static int getNextBPIdx( EC2DRV *obj )
{
	DUMP_FUNC();
	int i;
	
	for( i=0; i<4; i++ )
	{
		if( !( (obj->bp_flags)>>i)&0x01 )
			return i;				// not used, well take it
	}
	return -1;						// no more available
}

/** Get the index of the breakpoint for the specified address
  * \returns index of breakpoint matching supplied address or -1 if not found
  */
static int getBP( EC2DRV *obj, uint16_t addr )
{
	DUMP_FUNC();
	int i;

	for( i=0; i<4; i++ )
		if( ( obj->bpaddr[i]==addr) && ((obj->bp_flags>>i)&0x01) )
			return i;

	return -1;	// No active breakpoints with this address
}

// Modify the bp mask approprieatly and update EC2
/** Update both our local and the EC2 bp mask byte
  * \param bp		breakpoint number to update
  * \param active	TRUE set that bp active, FALSE to disable
  * \returns		TRUE = success, FALSE=failure
  */
static BOOL setBpMask( EC2DRV *obj, int bp, BOOL active )
{
	DUMP_FUNC();
	char cmd[7];
//	printf("static BOOL setBpMask( EC2DRV *obj, %i, %i )\n",bp,active);
//	printf("obj->bp_flags = 0x%04x\n",obj->bp_flags);
	if( active )
		obj->bp_flags |= ( 1 << bp );
	else
		obj->bp_flags &= ~( 1 << bp );
//	printf("obj->bp_flags = 0x%04x\n",obj->bp_flags);
	if( obj->mode==JTAG )
	{
		cmd[0] = 0x0D;
		cmd[1] = 0x05;
		cmd[2] = 0x86;
		cmd[3] = 0x10;
		cmd[4] = obj->bp_flags;
		cmd[5] = 0x00;
		cmd[6] = 0x00;
		if( trx( obj, cmd, 7, "\x0D", 1 ) )	// inform EC2
		{
			dump_bp(obj);
			return TRUE;
		}
		else
			return FALSE;
	}
	else if( obj->mode==C2 )
	{
		write_breakpoints_c2( obj );
		return TRUE;	// must succeed
	}
	return FALSE;
}

/** check the breakpoint flags to see if the specific breakpoint is set.
*/
BOOL isBPSet( EC2DRV *obj, int bpid )
{
	DUMP_FUNC();
	return (obj->bp_flags >> bpid) & 0x01;
}


/** cause the currently active breakpoints to be written to the device
	this is for c2 mode only as C2 mode dosen't store the breakpoints,
	they must all be written after each change.
*/
void write_breakpoints_c2( EC2DRV *obj )
{
	DUMP_FUNC();
	char cmd[4];
	int i;
	char bpregloc[] = { 0x85, 0xab, 0xce, 0xd2 };
	// preamble, seems to clear all the high order addresses and the bit7 associated with them
	trx( obj, "\x29\x86\x01\x00", 4, "\x0d", 1 );
	trx( obj, "\x29\xac\x01\x00", 4, "\x0d", 1 );
	trx( obj, "\x29\xcf\x01\x00", 4, "\x0d", 1 );
	trx( obj, "\x29\xd3\x01\x00", 4, "\x0d", 1 );
	
	// the normal breakpoints
	for( i=0; i<4; i++ )
	{
		if( isBPSet( obj, i ) )
		{
//			printf("C2: writing BP at 0x%04x, bpidx=%i\n",obj->bpaddr[i], i );
			cmd[0] = 0x29;
			cmd[1] = bpregloc[i];
			cmd[2] = 0x01;
			cmd[3] = obj->bpaddr[i]&0xff;			// low addr
			trx( obj, cmd, 4, "\x0d",1 );
			cmd[1] = bpregloc[i]+1;
			cmd[3] = (obj->bpaddr[i]>>8) | 0x80;	// high addr
			trx( obj, cmd, 4, "\x0d",1 );
		}
	}
}

/** Add a new breakpoint using the first available breakpoint
  */
BOOL ec2_addBreakpoint( EC2DRV *obj, uint16_t addr )
{
	DUMP_FUNC();
	char cmd[7];
	int bp;
//	printf("BOOL ec2_addBreakpoint( EC2DRV *obj, uint16_t addr )\n");
	if( getBP( obj, addr )==-1 )	// check address doesn't already have a BP
	{
		bp = getNextBPIdx( obj );
		if( bp!=-1 )
		{
			if( obj->mode==JTAG )
			{
//				printf("Adding breakpoint using jtag mode\n");
				// set address
				obj->bpaddr[bp] = addr;
				cmd[0] = 0x0D;
				cmd[1] = 0x05;
				cmd[2] = 0x90+bp;	// Breakpoint address register to write
				cmd[3] = 0x10;
				cmd[4] = addr & 0xFF;
				cmd[5] = (addr>>8) & 0xFF;
				cmd[6] = 0x00;
				if( !trx( obj, cmd, 7, "\x0D", 1 ) )
					return FALSE;
				return setBpMask( obj, bp, TRUE );
			}
			else if( obj->mode==C2 )
			{
//				printf("C2: Adding breakpoint into position %i\n",bp);
				obj->bpaddr[bp] = addr;
				return setBpMask( obj, bp, TRUE );
			}
			return TRUE;
		}
		else
			return FALSE;
	}
	else
		return FALSE;
}

BOOL ec2_removeBreakpoint( EC2DRV *obj, uint16_t addr )
{
	DUMP_FUNC();
	int16_t bp = getBP( obj, addr );
	if( bp != -1 )
		return setBpMask( obj, bp, FALSE );
	else
		return FALSE;
}


/**  Write the data pointed to by image into the flash memory of the EC2
  * \param image	buffer containing the firmware image.
  * \param len		Length of the image in bytes (shoulden't ever change)
  */
BOOL ec2_write_firmware( EC2DRV *obj, char *image, uint16_t len )
{
	DUMP_FUNC();
	int i;
	char cmd[4];
	BOOL r = FALSE;
	// defines order of captured blocks...
	const char ec2_block_order[] = 
	{ 
		0x0E,0x09,0x0D,0x05,0x06,0x0A,0x08,
		0x0C,0x0B,0x07,0x04,0x0F,0x02,0x03
	};
	const char ec3_block_order[] = 
	{ 
		0x11,0x12,0x1b,0x1d,
		0x1c,0x18,0x19,0x1a,
		0x0b,0x16,0x17,0x15,
		0x13,0x14,0x10,0x0c,
		0x0d,0x0e,0x0f,0x0c		// note 0x0c seems to be an end marker, why c again, there is no block for it!
	};
	
	if( obj->dbg_adaptor==EC2 )
	{
		update_progress( obj, 0 );
		ec2_reset( obj );
		trx( obj, "\x55", 1, "\x5A", 1 );
		for(i=0; i<14;i++)
		{
			cmd[0] = 0x01;
			cmd[1] = ec2_block_order[i];
			cmd[2] = 0x00;
			trx( obj, cmd, 3, "\x00", 1 );
			trx( obj, "\x02\x00\x00",3,"\x00", 1 );
			trx( obj, "\x03\x02\x00",3,"\x00", 1 );
			trx( obj, image+(i*0x200), 0x200, "\x00", 1 );
			write_port( obj, "\x04\x00\x00", 3 );
			read_port( obj, cmd, 2 );
			update_progress( obj, (i+1)*100/14 );
	//		printf("CRC = %02x%02x\n",(unsigned char)cmd[0],(unsigned char)cmd[1]);
		}
		ec2_reset( obj );
		r = trx( obj, "\x55", 1, "\x5a", 1 );
		ec2_reset( obj );
	}
	else if( obj->dbg_adaptor==EC3 )
	{
		//ec2_reset( obj );
		trx( obj, "\x05\x17\xff",3,"\xff",1);
		int i;
		for( i=0; i<19; i++)
		{
			cmd[0] = 0x01;
			cmd[1] = ec3_block_order[i];
			cmd[2] = 0x00;
			trx(obj,cmd,3,"\x00",1);
			trx(obj,"\x02\x00\x00",3,"\x00",1);
			trx(obj,"\x03\x02\x00",3,"\x00",1);
			// write the data block
			// 8 * 63 byte blocks
			// + 1 * 8 byte block
			int k;
			for(k=0; k<8; k++, image+=63 )
			{
				write_port( obj, image, 63 );
			}
			// not the 8 left over bytes 
			write_port( obj, image, 8 );
			read_port(obj, cmd, 2);
			image +=8;
			write_port(obj,"\x04\x00\x00",3);	// read back CRC
			read_port(obj,cmd,2);
		}
		
		
		trx(obj,"\x04\x00\x00",3,"\xb1\x37",2);	// CRC read
		trx(obj,"\x01\x0c\x00",3,"\x00",1);
		trx(obj,"\x06\x00\x00",3,"\x07",1);		// FW version
		ec2_target_reset(obj);
	}
	return r;
}


///////////////////////////////////////////////////////////////////////////////
/// Internal helper functions                                               ///
///////////////////////////////////////////////////////////////////////////////

/** Update progress counter and call callback if set
  */
inline static void update_progress( EC2DRV *obj, uint8_t percent )
{
	obj->progress = percent;
	if( obj->progress_cbk )
		obj->progress_cbk( obj->progress );

}

/** Send a block of characters to the port and check for the correct reply
  */
static BOOL trx( EC2DRV *obj, char *txbuf, int txlen, char *rxexpect, int rxlen )
{
	char rxbuf[256];
	write_port( obj, txbuf, txlen );
	if( read_port( obj, rxbuf, rxlen ) )
		return memcmp( rxbuf, rxexpect, rxlen )==0 ? TRUE : FALSE;
	else
		return FALSE;
}

/** Reset the EC2 by turning off DTR for a short period
  */
void ec2_reset( EC2DRV *obj )
{
	if( obj->dbg_adaptor==EC2 )
	{
		usleep(100);
		DTR( obj, FALSE );
		usleep(100);
		DTR( obj, TRUE );
		usleep(10000);	// 10ms minimum appears to be about 8ms so play it safe
	}
	else if( obj->dbg_adaptor==EC3 )
	{
		// fixme the following is unsave for some caller to ec2_reset
//		ec2_disconnect( obj );
//		ec2_connect( obj, obj->port );
		printf("ec2_reset C2\n");
	}
}

void init_ec2( EC2DRV *obj )
{
	EC2BLOCK init[] = {
	{ "\x04",1,"\x0D",1 },
	{ "\x1A\x06\x00\x00\x00\x00\x00\x00",8,"\x0D",1 },
	{ "\x0B\x02\x02\x00",4,"\x0D",1 },
	{ "\x14\x02\x10\x00",4,"\x04",1 },
	{ "\x16\x02\x01\x20",4,"\x01\x00",2 },
	
	{ "\x14\x02\x10\x00",4,"\x04",2 },
	{ "\x16\x02\x81\x20",4,"\x01\x00",2 },
	{ "\x14\x02\x10\x00",4,"\x04",1 },
	{ "\x16\x02\x81\x30",4,"\x01\x00",2 },
	{ "\x15\x02\x08\x00",4,"\x04",1 },
	
	{ "\x16\x01\xE0",3,"\x00",1 },
	{ "\x0B\x02\x01\x00",4,"\x0D",1 },
	{ "\x13\x00",2,"\x01",1 },
	{ "\x03\x02\x00\x00",4,"\x0D",1 },
	{ "\x0A\x00",2,"\x21\x01\x03\x00\x00\x12",6 },
	
	{ "\x10\x00",2,"\x07",1 },
	{ "\x0C\x02\x80\x12",4,"\x00\x07\x1C",3 },
	{ "\x02\x02\xB6\x01",4,"\x80",1 },
	{ "\x02\x02\xB2\x01",4,"\x14",1 },
	{ "\x03\x02\xB2\x04",4,"\x0D",1 },
	
	{ "\x0B\x02\x04\x00",4,"\x0D",1 },
	{ "\x0D\x05\x85\x08\x01\x00\x00",7,"\x0D",1 },
	{ "\x0D\x05\x84\x10\xFE\xFD\x00",7,"\x0D",1 },
	{ "\x0D\x05\x82\x08\x01\x00\x00",7,"\x0D",1 },
	{ "\x0D\x05\x84\x10\xFE\xFD\x00",7,"\x0D",1 },

	{ "\x11\x02\x01\x00",4,"\xFF",1 },
	{ "\x0D\x05\x82\x08\x00\x00\x00",7,"\x0D",1 },
	{ "\x0B\x02\x01\x00",4,"\x0D",1 },
	{ "\x03\x02\xB6\x80",4,"\x0D",1 },
	{ "\x03\x02\xB2\x14",4,"\x0D",1 },
	
	{ "\x02\x02\xB6\x01",4,"\x80",1 },
	{ "\x02\x02\xB2\x01",4,"\x14",1 },
	{ "\x03\x02\xB2\x04",4,"\x0D",1 },
	{ "\x0B\x02\x04\x00",4,"\x0D",1 },
	{ "\x0D\x05\x85\x08\x01\x00\x00",7,"\x0D",1 },
	
	{ "\x0D\x05\x84\x10\xFF\xFD\x00",7,"\x0D",1 },
	{ "\x0D\x05\x82\x08\x01\x00\x00",7,"\x0D",1 },
	{ "\x0D\x05\x84\x10\xFF\xFD\x00",7,"\x0D",1 },
	{ "\x11\x02\x01\x00",4,"\xFF",1 },
	{ "\x0D\x05\x82\x08\x00\x00\x00",7,"\x0D",1 },
	
	{ "\x0B\x02\x01\x00",4,"\x0D",1 },
	{ "\x03\x02\xB6\x80",4,"\x0D",1 },
	{ "\x03\x02\xB2\x14",4,"\x0D",1 },
	
	{ "",-1,"",-1 } };
	
	txblock( obj, init );
	ec2_clear_all_bp( obj );
}



BOOL txblock( EC2DRV *obj, EC2BLOCK *blk )
{
	int i = 0;
	while( blk[i].tlen != -1 )
	{
		trx( obj, blk[i].tx, blk[i].tlen, blk[i].rx, blk[i].rlen );
		i++;
	}
}


///////////////////////////////////////////////////////////////////////////////
/// COM port control functions                                              ///
///////////////////////////////////////////////////////////////////////////////
static BOOL open_port( EC2DRV *obj, const char *port )
{
	if( obj->dbg_adaptor==EC3 )
	{
		return open_ec3( obj, port );
	}
	else
	{
	obj->fd = open( port, O_RDWR | O_NOCTTY | O_NDELAY );
	if( obj->fd == -1 )
	{
		/*
		* Could not open the port.
		*/
		printf("open_port: Unable to open %s\n", port );
		return FALSE;
	}
	else
	{
		fcntl( obj->fd, F_SETFL, 0 );
		struct termios options;

		// Get the current options for the port...
		tcgetattr( obj->fd, &options );
		
		// Set the baud rates to 115200
		cfsetispeed(&options, B115200);
		cfsetospeed(&options, B115200);
//		cfsetispeed(&options, B57600);
//		cfsetospeed(&options, B57600);

		// Enable the receiver and set local mode...
		options.c_cflag |= (CLOCAL | CREAD);

		// set 8N1
		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;
		
		// Disable hardware flow control
		options.c_cflag &= ~CRTSCTS;
		
		// Disable software flow control
		options.c_iflag = 0;	// raw mode, no translations, no parity checking etc.
		
		// select RAW input
		options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

		// select raw output
		options.c_oflag &= ~OPOST;
		
		options.c_cc[VMIN] = 1;
		
		// Set the new options for the port...
		tcsetattr( obj->fd, TCSANOW, &options );
	}
	RTS( obj, TRUE );
	DTR( obj, TRUE );
	return TRUE;
	}
}

static BOOL write_port_ch( EC2DRV *obj, char ch )
{
	if( obj->dbg_adaptor==EC3 )
		return write_usb_ch( obj, ch );
	else
		return write_port( obj, &ch, 1 );
}

static BOOL write_port( EC2DRV *obj, char *buf, int len )
{
	if( obj->dbg_adaptor==EC3 )
	{
		return write_usb( obj, buf, len );
	}
	else
	{
		tx_flush( obj );
		rx_flush( obj );
		write( obj->fd, buf, len );
		tcdrain(obj->fd);
		usleep(10000);				// without this we get TIMEOUT errors
		if( obj->debug )
		{
			printf("TX: ");
			print_buf( buf, len );
		}
		return TRUE;
	}
}

static int read_port_ch( EC2DRV *obj )
{
	if( obj->dbg_adaptor==EC3 )
	{
		return read_usb_ch( obj );
	}
	else
	{
		char ch;
		if( read_port( obj, &ch, 1 ) )
			return ch;
		else
			return -1;
	}
}

static BOOL read_port( EC2DRV *obj, char *buf, int len )
{
	if( obj->dbg_adaptor==EC3 )
	{
		return read_usb( obj, buf, len );
	}
	else
	{
		fd_set			input;
		struct timeval	timeout;
		int cnt=0, r, n;
		while(TRUE)
		{
//			r = read( obj->fd, cur_ptr, len-cnt );
//			if( obj->debug )
			// Initialize the input set
			FD_ZERO( &input );
			FD_SET( obj->fd, &input );
			//			fcntl(obj->fd, F_SETFL, 0);	// block if not enough		characters available

			// Initialize the timeout structure
			timeout.tv_sec  = 5;		// n seconds timeout
			timeout.tv_usec = 0;
			
			// Do the select
			n = select( obj->fd+1, &input, NULL, NULL,&timeout );
			if (n < 0)
			{
//				printf("RX: ");
//				print_buf( buf, len );
				perror("select failed");
				exit(-1);
				return FALSE;
			}
			else if (n == 0)
			{
				puts("TIMEOUT");
				return FALSE;
			}
			else
			{
				r = read( obj->fd, buf+cnt, len-cnt );
				if (r < 1)
				{
					printf ("Problem !!! This souldn't happened.\n");
					return FALSE;
				}
				cnt += r;
				if( obj->debug )
				{
					printf("RX: ");
					print_buf( buf, len );
				}
				if (cnt == len)
				{
					return TRUE;
				}
			}
		}
	}
}


static void rx_flush( EC2DRV *obj )
{
	tcflush( obj->fd, TCIFLUSH );
}

static void tx_flush( EC2DRV *obj )
{
	tcflush( obj->fd, TCOFLUSH );
}

static void close_port( EC2DRV *obj )
{
	if( obj->dbg_adaptor==EC3 )
		close_ec3( obj );
	else
		close( obj->fd );
}

static void DTR( EC2DRV *obj, BOOL on )
{
	if( obj->dbg_adaptor==EC2 )
	{
		int status;
		ioctl( obj->fd, TIOCMGET, &status );
		if( on )
			status |= TIOCM_DTR;
		else
			status &= ~TIOCM_DTR;
		ioctl( obj->fd, TIOCMSET, &status );
	}
}

static void RTS( EC2DRV *obj, BOOL on )
{
	if( obj->dbg_adaptor==EC2 )
	{
		int status;
		ioctl( obj->fd, TIOCMGET, &status );
		if( on )
			status |= TIOCM_RTS;
		else
			status &= ~TIOCM_RTS;
		ioctl( obj->fd, TIOCMSET, &status );
	}
}

static void print_buf( char *buf, int len )
{
	while( len-- !=0 )
		printf("%02x ",(unsigned char)*buf++);
	printf("\n");
}


///////////////////////////////////////////////////////////////////////////////
/// EC3, USB control functions                                              ///
///////////////////////////////////////////////////////////////////////////////
#define EC3_OUT_ENDPOINT	0x02
#define EC3_IN_ENDPOINT		0x81
#define EC3_PRODUCT_ID		0x8044
#define EC3_VENDOR_ID		0x10c4
extern int usb_debug;		///< control libusb debugging

/* write a complete command to the EC3.
  adds length byte
*/
static BOOL write_usb( EC2DRV *obj, char *buf, int len )
{
	int r;
	char *txbuf = malloc( len + 1 );
	txbuf[0] = len;
	memcpy( txbuf+1, buf, len );
	if( obj->debug )
	{
		printf("TX: ");
		print_buf(txbuf,len+1);
	}
	r = usb_interrupt_write( obj->ec3, EC3_OUT_ENDPOINT, txbuf, len + 1, 1000 );
	free( txbuf );
	return r > 0;
}


/** write a single byte to the EC3 using USB.
	This should only be used for writes that have exactly 1 byte of data and 1 length byte.
 */
static BOOL write_usb_ch( EC2DRV *obj, char ch )
{
	return write_usb( obj, &ch, 1 );
}

/* read a complete result from the EC3.
  strips off length byte
*/
static BOOL read_usb( EC2DRV *obj, char *buf, int len )
{
	int r;
	char *rxbuf = malloc( len + 1 );
	r = usb_interrupt_read( obj->ec3, EC3_IN_ENDPOINT, rxbuf, len+1, 1000 );
	if( obj->debug )
	{
		printf("RX: ");
		print_buf(rxbuf,len+1);
	}
	memcpy( buf, rxbuf+1, len );
	free( rxbuf );
	return r > 0;
}

/** read a single byte from the EC3 using USB.
	This should only be used for replies that have exactly 1 byte of data and 1 length byte.
*/
static int read_usb_ch( EC2DRV *obj )
{
	char ch;
	if( read_usb( obj, &ch, 1 ) )
		return ch;
	else
		return -1;
}


/** Initialise communications with an EC3.
	Search for an EC3 then initialise communications with it.
*/
BOOL open_ec3( EC2DRV *obj, const char *port )
{
	struct usb_bus *busses;
	struct usb_bus *bus;
	struct usb_device_descriptor *ec3descr=0;
	struct usb_device *ec3dev;
	char s[255];
	BOOL match = FALSE;
	
	//usb_debug = 4;	// enable libusb debugging
	usb_init();
	usb_find_busses();
	usb_find_devices();
	busses = usb_get_busses(); 

	ec3dev = 0;
	for (bus = busses; bus; bus = bus->next)
	{
		struct usb_device *dev;
		for (dev = bus->devices; dev; dev = dev->next)
		{
			if( (dev->descriptor.idVendor==EC3_VENDOR_ID) &&
				(dev->descriptor.idProduct==EC3_PRODUCT_ID) )
			{
				if( port==0 )
				{
					ec3descr = &dev->descriptor;
					ec3dev = dev;
					match = TRUE;
					break;
				}
				else
				{
					obj->ec3 = usb_open(dev);
					usb_get_string_simple(obj->ec3, dev->descriptor.iSerialNumber, s, sizeof(s));
					// check for matching serial number
//					printf("s='%s'\n",s);
					usb_release_interface( obj->ec3, 0 );
					usb_close(obj->ec3);
					if( strcmp( s, port )==0 )
					{
						ec3descr = &dev->descriptor;
						ec3dev = dev;
						match = TRUE;
						break;
					}
				}
			}
		}
	}
	if( match == FALSE )
	{
		printf("MATCH FAILED, no suitable devices\n");
		return FALSE;
	}
//	printf("bMaxPacketSize0 = %i\n",ec3descr->bMaxPacketSize0);
//	printf("iManufacturer = %i\n",ec3descr->iManufacturer);
//	printf("idVendor = %04x\n",(unsigned int)ec3descr->idVendor);
//	printf("idProduct = %04x\n",(unsigned int)ec3descr->idProduct);
	obj->ec3 = usb_open(ec3dev);
//	printf("open ec3 = %i\n",obj->ec3);

//	printf("getting manufacturere string\n");
	usb_get_string_simple(obj->ec3, ec3descr->iManufacturer, s, sizeof(s));
//	printf("s='%s'\n",s);

	int r;
	usb_set_configuration( obj->ec3, 1 );
	
#ifdef HAVE_USB_DETACH_KERNEL_DRIVER_NP
	// On linux we force the inkernel drivers to release the device for us.	
	// can't do too much for other platforms as this function is platform specific
	usb_detach_kernel_driver_np( obj->ec3, 0);
#endif
	r = usb_claim_interface( obj->ec3, 0 );
	
#ifdef HAVE_USB_DETACH_KERNEL_DRIVER_NP
	// On linux we force the inkernel drivers to release the device for us.	
	// can't do too much for other platforms as this function is platform specific
r = usb_detach_kernel_driver_np( obj->ec3, 0);
#endif

	return TRUE;
}


void close_ec3( EC2DRV *obj )
{
	usb_detach_kernel_driver_np( obj->ec3, 0);
	usb_release_interface( obj->ec3, 0 );
	usb_close(obj->ec3);
}
