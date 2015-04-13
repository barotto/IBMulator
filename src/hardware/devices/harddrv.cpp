/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

/* IBM's proprietary XT-derived 8-bit interface.
 * It emulates only the commands and data transfer modes needed by the PS/1
 * model 2011 BIOS. Almost no error checking is performed, guest code is
 * supposed to be bug free and well behaving.
 * It could not work with PS/2 model 30 or SEGA TeraDrive BIOS.
 */

#include "ibmulator.h"
#include "filesys.h"
#include "harddrv.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/devices/systemboard.h"
#include "hardware/devices/dma.h"
#include "hardware/devices/pic.h"
#include <cstring>


HardDrive g_harddrv;

#define HDD_DMA_CHAN 3
#define HDD_IRQ      14

#define HDD_EXEC_TIME_US        500
#define HDD_SECT_IDFIELD_BYTES  59   //>25 but what is the real value?

#define HDD_DEFTIME_US 100

/*
   IBM HDD types 1-44

   Cyl.    Head    Sect.   Write    Land
                           p-comp   Zone
*/
const HDDType HardDrive::ms_hdd_types[45] = {
{   0,     0,       0,          0,      0}, // 0 (none)
{ 306,     4,      17,        128,    305}, // 1 10MB
{ 615,     4,      17,        300,    615}, // 2 20MB
{ 615,     6,      17,        300,    615}, // 3 31MB
{ 940,     8,      17,        512,    940}, // 4 62MB
{ 940,     6,      17,        512,    940}, // 5 47MB
{ 615,     4,      17,         -1,    615}, // 6 20MB
{ 462,     8,      17,        256,    511}, // 7 31MB
{ 733,     5,      17,         -1,    733}, // 8 30MB
{ 900,    15,      17,         -1,    901}, // 9 112MB
{ 820,     3,      17,         -1,    820}, //10 20MB
{ 855,     5,      17,         -1,    855}, //11 35MB
{ 855,     7,      17,         -1,    855}, //12 50MB
{ 306,     8,      17,        128,    319}, //13 20MB
{ 733,     7,      17,         -1,    733}, //14 43MB
{   0,     0,       0,          0,      0}, //15 (reserved)
{ 612,     4,      17,          0,    663}, //16 20MB
{ 977,     5,      17,        300,    977}, //17 41MB
{ 977,     7,      17,         -1,    977}, //18 57MB
{ 1024,    7,      17,        512,   1023}, //19 59MB
{ 733,     5,      17,        300,    732}, //20 30MB
{ 733,     7,      17,        300,    732}, //21 43MB
{ 733,     5,      17,        300,    733}, //22 30MB
{ 306,     4,      17,          0,    336}, //23 10MB
{ 612,     4,      17,        305,    663}, //24 20MB
{ 306,     4,      17,         -1,    340}, //25 10MB
{ 612,     4,      17,         -1,    670}, //26 20MB
{ 698,     7,      17,        300,    732}, //27 41MB
{ 976,     5,      17,        488,    977}, //28 40MB
{ 306,     4,      17,          0,    340}, //29 10MB
{ 611,     4,      17,        306,    663}, //30 20MB
{ 732,     7,      17,        300,    732}, //31 43MB
{ 1023,    5,      17,         -1,   1023}, //32 42MB
{ 614,     4,      25,         -1,    663}, //33 30MB
{ 775,     2,      27,         -1,    900}, //34 20MB
{ 921,     2,      33,         -1,   1000}, //35 30MB
{ 402,     4,      26,         -1,    460}, //36 20MB
{ 580,     6,      26,         -1,    640}, //37 44MB
{ 845,     2,      36,         -1,   1023}, //38 30MB
{ 769,     3,      36,         -1,   1023}, //39 41MB
{ 531,     4,      39,         -1,    532}, //40 40MB
{ 577,     2,      36,         -1,   1023}, //41 20MB
{ 654,     2,      32,         -1,    674}, //42 20MB
{ 923,     5,      36,         -1,   1023}, //43 81MB
{ 531,     8,      39,         -1,    532}  //44 81MB
};

//Attachment Status Reg bits
#define HDD_ASR_TX_EN    0x1
#define HDD_ASR_INT_REQ  0x2
#define HDD_ASR_BUSY     0x4
#define HDD_ASR_DIR      0x8
#define HDD_ASR_DATA_REQ 0x10

//Attention Reg bits
#define HDD_ATT_DATA 0x10
#define HDD_ATT_SSB  0x20
#define HDD_ATT_CSB  0x40
#define HDD_ATT_CCB  0x80

//Attachment Control Reg bits
#define HDD_ACR_DMA_EN 0x1
#define HDD_ACR_INT_EN 0x2
#define HDD_ACR_RESET  0x80

//Interrupt Status Reg bits
#define HDD_ISR_CMD_REJECT  0x20
#define HDD_ISR_INVALID_CMD 0x40
#define HDD_ISR_TERMINATION 0x80

//CCB commands
enum HDD_CMD {
	READ_DATA   = 0x1,
	READ_CHECK  = 0x2,
	READ_EXT    = 0x3,
	READ_ID     = 0x5,
	RECALIBRATE = 0x8,
	WRITE_DATA  = 0x9,
	WRITE_VFY   = 0xA,
	WRITE_EXT   = 0xB,
	FORMAT_DISK = 0xD,
	SEEK        = 0xE,
	FORMAT_TRK  = 0xF
};

const std::function<void(HardDrive&)> HardDrive::ms_cmd_funcs[0xF+1] = {
	&HardDrive::undefined_cmd,    //0x0
	&HardDrive::read_data_cmd,    //0x1
	&HardDrive::read_check_cmd,   //0x2
	&HardDrive::read_ext_cmd,     //0x3
	&HardDrive::undefined_cmd,    //0x4
	&HardDrive::read_id_cmd,      //0x5
	&HardDrive::undefined_cmd,    //0x6
	&HardDrive::undefined_cmd,    //0x7
	&HardDrive::recalibrate_cmd,  //0x8
	&HardDrive::write_data_cmd,   //0x9
	&HardDrive::write_vfy_cmd,    //0xA
	&HardDrive::write_ext_cmd,    //0xB
	&HardDrive::undefined_cmd,    //0xC
	&HardDrive::format_disk_cmd,  //0xD
	&HardDrive::seek_cmd,         //0xE
	&HardDrive::format_trk_cmd    //0xF
};

//SSB bits
#define HDD_SSB_B0_B_NR 7 //not ready;
#define HDD_SSB_B0_B_SE 6 //seek end;
#define HDD_SSB_B0_B_WF 4 //write fault;
#define HDD_SSB_B0_B_CE 3 //cylinder error;
#define HDD_SSB_B0_B_T0 0 //on track 0
#define HDD_SSB_B1_B_EF 7 //error is on ID field
#define HDD_SSB_B1_B_ET 6 //error occurred
#define HDD_SSB_B1_B_AM 5 //address mark not found
#define HDD_SSB_B1_B_BT 4 //ID field with all bits set detected.
#define HDD_SSB_B1_B_WC 3 //cylinder bytes read did not match the cylinder requested in the CCB
#define HDD_SSB_B1_B_ID 0 //ID match not found
#define HDD_SSB_B2_B_RR 6 //reset needed
#define HDD_SSB_B2_B_RG 5 //read or write retry corrected the error
#define HDD_SSB_B2_B_DS 4 //defective sector bit in the ID field is 1.

#define HDD_SSB_B0_SE (1 << HDD_SSB_B0_B_SE)
#define HDD_SSB_B0_CE (1 << HDD_SSB_B0_B_CE)
#define HDD_SSB_B0_T0 (1 << HDD_SSB_B0_B_T0)
#define HDD_SSB_B1_WC (1 << HDD_SSB_B1_B_WC)
#define HDD_SSB_B2_RR (1 << HDD_SSB_B2_B_RR)

void HardDrive::State::CCB::set(uint8_t* _data)
{
	valid = true;

	command = _data[0] >> 4;
	no_data = (_data[0] >> 3) & 1; //ND
	auto_seek = (_data[0] >> 2) & 1; //AS
	park = _data[0] & 1; //P
	head = _data[1] >> 4;
	cylinder = ((_data[1] & 3) << 8) + _data[2];
	sector = _data[3];
	num_sectors = _data[5];

	PDEBUGF(LOG_V2, LOG_HDD, "command: ");
	switch(command) {
		case HDD_CMD::READ_DATA:   { PDEBUGF(LOG_V2, LOG_HDD, "READ_DATA "); break; }
		case HDD_CMD::READ_CHECK:  { PDEBUGF(LOG_V2, LOG_HDD, "READ_CHECK "); break; }
		case HDD_CMD::READ_EXT:    { PDEBUGF(LOG_V2, LOG_HDD, "READ_EXT "); break; }
		case HDD_CMD::READ_ID:     { PDEBUGF(LOG_V2, LOG_HDD, "READ_ID "); break; }
		case HDD_CMD::RECALIBRATE: { PDEBUGF(LOG_V2, LOG_HDD, "RECALIBRATE "); break; }
		case HDD_CMD::WRITE_DATA:  { PDEBUGF(LOG_V2, LOG_HDD, "WRITE_DATA "); break; }
		case HDD_CMD::WRITE_VFY:   { PDEBUGF(LOG_V2, LOG_HDD, "WRITE_VFY "); break; }
		case HDD_CMD::WRITE_EXT:   { PDEBUGF(LOG_V2, LOG_HDD, "WRITE_EXT "); break; }
		case HDD_CMD::FORMAT_DISK: { PDEBUGF(LOG_V2, LOG_HDD, "FORMAT_DISK "); break; }
		case HDD_CMD::SEEK:        { PDEBUGF(LOG_V2, LOG_HDD, "SEEK "); break; }
		case HDD_CMD::FORMAT_TRK:  { PDEBUGF(LOG_V2, LOG_HDD, "FORMAT_TRK "); break; }
		default:
			PDEBUGF(LOG_V2, LOG_HDD, "invalid!\n");
			valid = false;
			return;
	}

	PDEBUGF(LOG_V2, LOG_HDD, " C:%d,H:%d,S:%d,nS:%d\n",
			cylinder, head, sector, num_sectors);
}

void HardDrive::State::SSB::copy_to(uint8_t *_dest)
{
	_dest[0] = not_ready << HDD_SSB_B0_B_NR;
	_dest[0] |= seek_end << HDD_SSB_B0_B_SE;
	_dest[0] |= cylinder_err << HDD_SSB_B0_B_CE;
	_dest[0] |= track_0 << HDD_SSB_B0_B_T0;
	_dest[1] = 0;
	_dest[2] = reset << HDD_SSB_B2_B_RR;
	_dest[3] = last_cylinder & 0xFF;
	_dest[4] = ((last_cylinder & 0x300) >> 3) + last_head;
	_dest[5] = last_sector;
	_dest[6] = 0x2; //sector size: the value is always hex 02 to indicate 512 bytes.
	_dest[7] = (present_head << 4) + ((present_cylinder & 0x300)>>8);
	_dest[8] = present_cylinder & 0xFF;
	_dest[9] = 0;
	_dest[10] = 0;
	_dest[11] = command_syndrome;
	_dest[12] = drive_type;
	_dest[13] = 0;
}

void HardDrive::State::SSB::clear()
{
	not_ready = false;
	seek_end = false;
	cylinder_err = false;
	track_0 = false;
	reset = false;
	present_head = 0;
	present_cylinder = 0;
	last_head = 0;
	last_cylinder = 0;
	last_sector = 0;
	command_syndrome = 0;

	//drive_type is static
}

unsigned HardDrive::chs_to_lba(unsigned _c, unsigned _h, unsigned _s) const
{
	ASSERT(_s>0);
	return (_c * m_disk->heads + _h ) * m_disk->spt + (_s-1);
}

void HardDrive::lba_to_chs(unsigned _lba, unsigned &_c, unsigned &_h, unsigned &_s) const
{
	_c = _lba / (m_disk->heads * m_disk->spt);
	_h = (_lba / m_disk->spt) % m_disk->heads;
	_s = (_lba % m_disk->spt) + 1;
}

HardDrive::HardDrive()
{
}

HardDrive::~HardDrive()
{
	unmount();
}

void HardDrive::init()
{
	g_dma.register_8bit_channel(3,
			std::bind(&HardDrive::dma_read, this, std::placeholders::_1, std::placeholders::_2),
			std::bind(&HardDrive::dma_write, this, std::placeholders::_1, std::placeholders::_2),
			get_name());
	g_machine.register_irq(HDD_IRQ, get_name());

	g_devices.register_read_handler(this, 0x0320, 1);  //Data Reg
	g_devices.register_write_handler(this, 0x0320, 1); //Data Reg
	g_devices.register_read_handler(this, 0x0322, 1);  //Attachment Status Reg
	g_devices.register_write_handler(this, 0x0322, 1); //Attachment Control Reg
	g_devices.register_read_handler(this, 0x0324, 1);  //Interrupt Status Reg
	g_devices.register_write_handler(this, 0x0324, 1); //Attention Reg

	m_cmd_timer = g_machine.register_timer(
			std::bind(&HardDrive::cmd_timer,this),
			100,    // period usec
			false,  // continuous
			false,  // active
			"HDD-cmd"//name
	);
	m_dma_timer = g_machine.register_timer(
			std::bind(&HardDrive::dma_timer,this),
			100,    // period usec
			false,  // continuous
			false,  // active
			"HDD-dma"//name
	);

	//get_enum throws if value is not allowed:
	m_drive_type = g_program.config().get_int(DRIVES_SECTION, DRIVES_HDD);
	if(m_drive_type<0 || m_drive_type == 15 || m_drive_type > 44) {
		PERRF(LOG_HDD, "Invalid HDD type %d\n", m_drive_type);
		throw std::exception();
	}
	m_original_type = m_drive_type;
	if(m_drive_type > 0) {
		std::string imgpath = g_program.config().find_media(DISK_C_SECTION, DISK_PATH);
		mount(imgpath);
		m_write_protect = g_program.config().get_bool(DISK_C_SECTION, DISK_READONLY);
		m_original_path = m_disk->get_name();
		m_save_on_close = g_program.config().get_bool(DISK_C_SECTION, DISK_SAVE);
		PINFOF(LOG_V0, LOG_HDD, "Installed drive C as type %d (%.1fMiB)\n",
				m_drive_type, double(m_disk->hd_size)/(1024.0*1024.0));
	} else {
		PINFOF(LOG_V0, LOG_HDD, "Drive C not installed\n");
	}
}

void HardDrive::reset(unsigned _type)
{
	if(_type == MACHINE_POWER_ON || _type==MACHINE_HARD_RESET) {
		memset(&m_s, 0, sizeof(m_s));
		m_s.ssb.drive_type = m_drive_type;
	}

	lower_interrupt();
}

void HardDrive::config_changed()
{
	unmount();
	//get_enum throws if value is not allowed:
	m_drive_type = g_program.config().get_int(DRIVES_SECTION, DRIVES_HDD);
	if(m_drive_type<0 || m_drive_type == 15 || m_drive_type > 44) {
		PERRF(LOG_HDD, "Invalid HDD type %d\n", m_drive_type);
		throw std::exception();
	}
	//disk mount is performed at restore_state
}

void HardDrive::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "saving state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	if(m_disk) {
		std::string path = _state.get_basename() + "-hdd.img";
		m_disk->save_state(path.c_str());
	}
}

void HardDrive::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "restoring state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	if(m_drive_type != 0) {
		//the saved state is read only
		g_program.config().set_bool(DISK_C_SECTION, DISK_READONLY, true);
		mount(_state.get_basename() + "-hdd.img");
	}
}

void HardDrive::mount(std::string _imgpath)
{
	if(_imgpath.empty()) {
		PERRF(LOG_HDD, "You need to specify a HDD image file\n");
		throw std::exception();
	}
	if(FileSys::is_directory(_imgpath.c_str())) {
		PERRF(LOG_HDD, "Invalid HDD image file\n");
		throw std::exception();
	}

	m_tmp_disk = false;

	uint32_t seek_max=40000;
	double tx_rate_mbps;
	int rpm,interleave=4;

	//the only performance values I have are those of type 35 and 38
	if(m_drive_type == 38) {
		tx_rate_mbps = 10.8; //disk-to-buffer?
		m_trk2trk_us = 9000;
		rpm = 3700;
	} else if(m_drive_type>0 && m_drive_type!=15 && m_drive_type<45) {
		//disk type 35 values
		tx_rate_mbps = 10.2;
		m_trk2trk_us = 8000;
		rpm = 3600;
	} else {
		PERRF(LOG_HDD, "Invalid drive type %d!\n", m_drive_type);
		throw std::exception();
	}
	const HDDType &type = ms_hdd_types[m_drive_type];
	//equivalent to x / ((tx_rate_mbps*1e6)/8.0) / 1e6 :
	m_sec_tx_us = ((512+HDD_SECT_IDFIELD_BYTES)*interleave) / (tx_rate_mbps/8.0);
	m_exec_time_us = HDD_EXEC_TIME_US;
	m_sectors = type.spt * type.cylinders * type.heads;
	m_avg_rot_lat = 30000000/rpm; //average, the maximum is twice this value
	m_avg_trk_lat_us = (seek_max - m_avg_rot_lat) / type.cylinders;

	m_disk = std::unique_ptr<FlatMediaImage>(new FlatMediaImage());
	m_disk->spt = type.spt;
	m_disk->cylinders = type.cylinders;
	m_disk->heads = type.heads;

	if(!FileSys::file_exists(_imgpath.c_str())) {
		PINFOF(LOG_V0, LOG_HDD, "Creating new image file '%s'\n", _imgpath.c_str());
		if(m_drive_type == 35) {
			std::string imgsrc = g_program.config().get_file_path("hdd.img.zip",FILE_TYPE_ASSET);
			if(!FileSys::file_exists(imgsrc.c_str())) {
				PERRF(LOG_HDD, "Cannot find the image file archive\n");
				throw std::exception();
			}
			if(!FileSys::extract_file(imgsrc.c_str(), "hdd.img", _imgpath.c_str())) {
				PERRF(LOG_HDD, "Cannot find the image file in the archive\n");
				throw std::exception();
			}
		} else {
			//create a new image
			try {
				m_disk->create(_imgpath.c_str(), m_sectors);
			} catch(std::exception &e) {
				PERRF(LOG_HDD, "Unable to create the image file\n");
				throw;
			}
		}
	} else {
		PINFOF(LOG_V0, LOG_HDD, "Using image file '%s'\n", _imgpath.c_str());
	}

	if(g_program.config().get_bool(DISK_C_SECTION, DISK_READONLY)
	   || !FileSys::is_file_writeable(_imgpath.c_str()))
	{
		PINFOF(LOG_V1, LOG_HDD, "The image file is read-only, using a replica\n");

		std::string dir, base, ext;
		if(!FileSys::get_path_parts(_imgpath.c_str(), dir, base, ext)) {
			PERRF(LOG_HDD, "Error while determining the image file path\n");
			throw std::exception();
		}
		std::string tpl = g_program.config().get_cfg_home()
		                + FS_SEP + base + "-XXXXXX";

		//opening a temp image
		//this works in C++11, where strings are guaranteed to be contiguous:
		if(dynamic_cast<FlatMediaImage*>(m_disk.get())->open_temp(_imgpath.c_str(), &tpl[0]) < 0) {
			PERRF(LOG_HDD, "Can't open the image file\n");
			throw std::exception();
		}
		m_tmp_disk = true;
	} else {
		if(m_disk->open(_imgpath.c_str()) < 0) {
			PERRF(LOG_HDD, "Error opening the image file\n");
			throw std::exception();
		}
	}
}

void HardDrive::unmount()
{
	if(!m_disk || !m_disk->is_open()) {
		return;
	}

	if(m_tmp_disk && m_save_on_close && m_drive_type==m_original_type && !m_write_protect) {
		if(!FileSys::file_exists(m_original_path.c_str())
		   || FileSys::is_file_writeable(m_original_path.c_str()))
		{
			//make the current disk state permanent.
			m_disk->save_state(m_original_path.c_str());
		}
	}

	m_disk->close();
	if(m_tmp_disk) {
		remove(m_disk->get_name().c_str());
	}
	m_disk.reset(nullptr);
}

uint16_t HardDrive::read(uint16_t _address, unsigned)
{
	if(!m_disk) {
		return ~0;
	}

	PDEBUGF(LOG_V2, LOG_HDD, "read  0x%04X ", _address);

	//set the Card Selected Feedback bit
	g_sysboard.set_feedback();

	uint16_t value = 0;
	switch(_address) {
		case 0x320:
			//Data Reg
			if(!(m_s.attch_status_reg & HDD_ASR_DATA_REQ)) {
				PDEBUGF(LOG_V2, LOG_HDD, "null data read\n");
				break;
			}
			if(!(m_s.attch_status_reg & HDD_ASR_DIR)) {
				PDEBUGF(LOG_V2, LOG_HDD, "wrong data dir\n");
				break;
			}
			ASSERT(m_s.data_size);
			m_s.attch_status_reg |= HDD_ASR_TX_EN;
			value = m_s.data_stack[m_s.data_ptr];
			PDEBUGF(LOG_V2, LOG_HDD, "data %02d/%02d   -> 0x%04X\n", m_s.data_ptr,
					(m_s.data_size-1), value);
			m_s.data_ptr++;
			if(m_s.data_ptr >= m_s.data_size) {
				m_s.attch_status_reg &= ~HDD_ASR_TX_EN;
				m_s.attch_status_reg &= ~HDD_ASR_DATA_REQ;
				m_s.attch_status_reg &= ~HDD_ASR_DIR;
				m_s.data_size = 0;
				m_s.data_ptr = 0;
				//TODO non-DMA sector data transfers
			}
			break;
		case 0x322:
			//Attachment Status Reg
			//This register contains status information on the present state of
			//the controller.
			value = m_s.attch_status_reg;
			PDEBUGF(LOG_V2, LOG_HDD, "attch status -> 0x%04X ", value);
			if(value & HDD_ASR_TX_EN) { PDEBUGF(LOG_V2, LOG_HDD, "TX_EN "); }
			if(value & HDD_ASR_INT_REQ) { PDEBUGF(LOG_V2, LOG_HDD, "INT_REQ "); }
			if(value & HDD_ASR_BUSY) { PDEBUGF(LOG_V2, LOG_HDD, "BUSY "); }
			if(value & HDD_ASR_DIR) { PDEBUGF(LOG_V2, LOG_HDD, "DIR "); }
			if(value & HDD_ASR_DATA_REQ) { PDEBUGF(LOG_V2, LOG_HDD, "DATA_REQ "); }
			PDEBUGF(LOG_V2, LOG_HDD, "\n");
			break;
		case 0x324:
			//Interrupt Status Reg
			//At the end of all commands from the microprocessor, the disk
			//controller returns completion status information to this register.
			//This byte informs the system if an error occurred during the
			//execution of the command.
			value = m_s.int_status_reg;
			PDEBUGF(LOG_V2, LOG_HDD, "int status   -> 0x%04X\n", value);
			m_s.int_status_reg = 0; //<--- TODO is it correct?
			//Int req bit is cleared when this register is read:
			m_s.attch_status_reg &= ~HDD_ASR_INT_REQ;
			//lower_interrupt(); //TODO
			break;
		default:
			PERRF(LOG_HDD, "unhandled read!\n", _address);
			break;
	}
	return value;
}

void HardDrive::write(uint16_t _address, uint16_t _value, unsigned)
{
	if(!m_disk) {
		return;
	}

	PDEBUGF(LOG_V2, LOG_HDD, "write 0x%04X ", _address);

	//set the Card Selected Feedback bit
	g_sysboard.set_feedback();

	switch(_address) {
		case 0x320:
			//Data Reg
			if(!(m_s.attch_status_reg & HDD_ASR_DATA_REQ)) {
				PDEBUGF(LOG_V2, LOG_HDD, "null data write\n");
				break;
			}
			if(m_s.attch_status_reg & HDD_ASR_DIR) {
				PDEBUGF(LOG_V2, LOG_HDD, "wrong data dir\n");
				break;
			}
			ASSERT(m_s.data_size);
			m_s.attch_status_reg |= HDD_ASR_TX_EN;
			PDEBUGF(LOG_V2, LOG_HDD, "data %02d/%02d   <- 0x%04X\n", m_s.data_ptr,
					(m_s.data_size-1), _value);
			m_s.data_stack[m_s.data_ptr] = _value;
			m_s.data_ptr++;
			if(m_s.data_ptr >= m_s.data_size) {
				m_s.attch_status_reg &= ~HDD_ASR_TX_EN;
				m_s.attch_status_reg &= ~HDD_ASR_DATA_REQ;
				m_s.data_size = 0;
				m_s.data_ptr = 0;
				if(m_s.attention_reg) {
					attention();
				}
			}
			break;
		case 0x322:
			//Attachment Control Reg
			//The Attachment Control register controls the fixed-disk interrupt
			//and DMA channel, and resets the drive.
			PDEBUGF(LOG_V2, LOG_HDD, "attch ctrl   <- 0x%04X ", _value);
			if(_value & HDD_ACR_DMA_EN) { PDEBUGF(LOG_V2, LOG_HDD, "DMA_EN "); }
			if(_value & HDD_ACR_INT_EN) { PDEBUGF(LOG_V2, LOG_HDD, "INT_EN "); }
			if(_value & HDD_ACR_RESET) { PDEBUGF(LOG_V2, LOG_HDD, "RESET "); }
			PDEBUGF(LOG_V2, LOG_HDD, "\n");
			m_s.attch_ctrl_reg = _value;
			if(!(_value & HDD_ACR_INT_EN)) {
				lower_interrupt();
			}
			if(m_s.reset_phase) {
				m_s.reset_phase++;
				if(m_s.reset_phase == 3) {
					raise_interrupt();
					m_s.reset_phase = 0;
				}
				break;
			}
			if(_value & HDD_ACR_RESET) {
				reset(MACHINE_HARD_RESET);
				m_s.reset_phase = 1;
				break;
			}
			break;
		case 0x324:
			//Attention Reg
			//The system uses this register to initiate all transactions with
			//the drive.
			PDEBUGF(LOG_V2, LOG_HDD, "attention    <- 0x%04X ", _value);
			if(_value & HDD_ATT_DATA) { PDEBUGF(LOG_V2, LOG_HDD, "DATA "); }
			if(_value & HDD_ATT_SSB) { PDEBUGF(LOG_V2, LOG_HDD, "SSB "); }
			if(_value & HDD_ATT_CSB) { PDEBUGF(LOG_V2, LOG_HDD, "CSB "); }
			if(_value & HDD_ATT_CCB) { PDEBUGF(LOG_V2, LOG_HDD, "CCB "); }
			PDEBUGF(LOG_V2, LOG_HDD, "\n");
			if(_value & HDD_ATT_DATA) {
				if(!(m_s.attch_status_reg & HDD_ASR_DATA_REQ)) {
					//data is not ready, what TODO ?
					PERRF_ABORT(LOG_HDD, "data not ready\n");
				}
				if(m_s.attch_ctrl_reg & HDD_ACR_DMA_EN) {
					g_machine.activate_timer(m_dma_timer, HDD_TIMING?m_exec_time_us:HDD_DEFTIME_US, 0);
				}
			} else if(_value & HDD_ATT_SSB) {
				m_s.attention_reg |= HDD_ATT_SSB;
				attention();
			} else if(_value & HDD_ATT_CCB) {
				m_s.data_ptr = 0;
				m_s.data_size = 6;
				m_s.attch_status_reg |= HDD_ASR_DATA_REQ;
				m_s.attention_reg |= HDD_ATT_CCB;
			}
			break;
		default:
			PERRF(LOG_HDD, "unhandled write!\n", _address);
			break;
	}

}

void HardDrive::command()
{
	uint32_t time_us = m_exec_time_us + m_avg_rot_lat;

	if(m_s.ccb.auto_seek) {
		time_us += get_seek_time(m_s.ccb.cylinder);
	}
	unsigned s = m_s.ccb.sector;
	switch(m_s.ccb.command) {
		case HDD_CMD::WRITE_DATA:
			m_s.attch_status_reg |= HDD_ASR_DATA_REQ;
			m_s.data_size = 512;
			m_s.data_ptr = 0;
			break;
		case HDD_CMD::READ_DATA:
			break;
		case HDD_CMD::READ_CHECK:
			time_us += m_sec_tx_us*m_s.ccb.num_sectors;
			break;
		case HDD_CMD::SEEK:
			s = 0;
			if(!m_s.ccb.park) {
				time_us = m_exec_time_us + get_seek_time(m_s.ccb.cylinder);
			}
			break;
		default:
			//time needed to read the first sector
			time_us += m_sec_tx_us;
			break;
	}
	set_cur_sector(m_s.ccb.head, s);
	m_s.attch_status_reg |= HDD_ASR_BUSY;
	g_machine.activate_timer(m_cmd_timer, HDD_TIMING?time_us:HDD_DEFTIME_US, 0);

	PDEBUGF(LOG_V2, LOG_HDD, "command exec, busy for %d usecs\n", time_us);
}

uint32_t HardDrive::get_seek_time(unsigned _c)
{
	uint32_t time = 0;
	if(m_s.cur_cylinder != _c) {
		int dc = abs(int(m_s.cur_cylinder) - int(_c));
		time = m_trk2trk_us + dc*m_avg_trk_lat_us;
	}
	return time;
}

void HardDrive::attention()
{
	if(m_s.attention_reg & HDD_ATT_CCB) {
		m_s.ccb.set(m_s.data_stack);
		if(!m_s.ccb.valid) {
			m_s.int_status_reg |= HDD_ISR_INVALID_CMD;
			raise_interrupt();
		} else {
			command();
		}
	} else if(m_s.attention_reg & HDD_ATT_SSB) {
		m_s.attention_reg &= ~HDD_ATT_SSB;
		if(!m_s.ssb.valid) {
			m_s.ssb.clear();
			m_s.ssb.last_cylinder = m_s.cur_cylinder; //TODO?
			m_s.ssb.last_head = m_s.cur_head; //TODO?
			m_s.ssb.last_sector = m_s.cur_sector; //TODO?
			m_s.ssb.present_cylinder = m_s.cur_cylinder;
			m_s.ssb.present_head = m_s.cur_head;
			m_s.ssb.track_0 = (m_s.cur_cylinder == 0);
		}
		m_s.ssb.copy_to(m_s.data_stack);
		fill_data_stack(nullptr, 14);
		m_s.attch_status_reg |= HDD_ASR_DIR;
		raise_interrupt();
		m_s.ssb.valid = false;
	}
}

void HardDrive::raise_interrupt()
{
	m_s.attch_status_reg |= HDD_ASR_INT_REQ;
	if(m_s.attch_ctrl_reg & HDD_ACR_INT_EN) {
		PDEBUGF(LOG_V2, LOG_HDD, "raising IRQ %d\n", HDD_IRQ);
		g_pic.raise_irq(HDD_IRQ);
	}
}

void HardDrive::lower_interrupt()
{
	g_pic.lower_irq(HDD_IRQ);
}

void HardDrive::fill_data_stack(uint8_t *_source, unsigned _len)
{
	ASSERT(_len<=512);

	if(_source != nullptr) {
		memcpy(m_s.data_stack, _source, _len);
	}
	m_s.data_ptr = 0;
	m_s.data_size = _len;
	m_s.attch_status_reg |= HDD_ASR_DATA_REQ;
}

uint16_t HardDrive::dma_write(uint8_t *_buffer, uint16_t _maxlen)
{
	// A DMA write is from I/O to Memory
	// We need to return the next data byte(s) from the buffer
	// to be transfered via the DMA to memory.
	//
	// maxlen is the maximum length of the DMA transfer

	//TODO implement control blocks DMA transfers?
	ASSERT(m_s.ccb.valid);
	ASSERT(m_s.attch_status_reg & HDD_ASR_DATA_REQ);
	ASSERT(m_s.attch_status_reg & HDD_ASR_DIR);

	g_sysboard.set_feedback();

	uint16_t len = m_s.data_size - m_s.data_ptr;
	if(len > _maxlen) {
		len = _maxlen;
	}
	PDEBUGF(LOG_V2, LOG_HDD, "DMA write: %d bytes of %d (%d requested)\n",
			len, (m_s.data_size - m_s.data_ptr),_maxlen);
	memcpy(_buffer, &m_s.data_stack[m_s.data_ptr], len);
	m_s.data_ptr += len;
	bool TC = g_dma.get_TC() && (len == _maxlen);

	if((m_s.data_ptr >= m_s.data_size) || TC) {

		if(m_s.data_ptr >= m_s.data_size) {
			m_s.data_ptr = 0;
		}
		if(TC) { // Terminal Count line, done
			PDEBUGF(LOG_V2, LOG_HDD, "<<DMA WRITE TC>> C:%d,H:%d,S:%d,nS:%d\n",
					m_s.cur_cylinder, m_s.cur_head,
					m_s.cur_sector, m_s.ccb.num_sectors);
			m_s.attch_status_reg &= ~HDD_ASR_DATA_REQ;
			m_s.attch_status_reg &= ~HDD_ASR_DIR;
			raise_interrupt();
		} else { // more data to transfer
			m_s.attch_status_reg |= HDD_ASR_BUSY;
			g_machine.activate_timer(m_cmd_timer, HDD_TIMING?m_sec_tx_us:HDD_DEFTIME_US, 0);
		}
		g_dma.set_DRQ(HDD_DMA_CHAN, 0);
	}
	return len;
}

uint16_t HardDrive::dma_read(uint8_t *_buffer, uint16_t _maxlen)
{
	/*
	 * From Memory to I/O
	 */
	g_sysboard.set_feedback();

	//TODO implement control blocks DMA transfers?
	ASSERT(m_s.ccb.valid);
	ASSERT(m_s.attch_status_reg & HDD_ASR_DATA_REQ);
	ASSERT(!(m_s.attch_status_reg & HDD_ASR_DIR));

	uint16_t len = m_s.data_size - m_s.data_ptr;
	if(len > _maxlen) {
		len = _maxlen;
	}
	PDEBUGF(LOG_V2, LOG_HDD, "DMA read: %d bytes of %d (%d to send)\n",
			len, (m_s.data_size - m_s.data_ptr),_maxlen);
	memcpy(&m_s.data_stack[m_s.data_ptr], _buffer, len);
	m_s.data_ptr += len;
	bool TC = g_dma.get_TC() && (len == _maxlen);
	if((m_s.data_ptr >= m_s.data_size) || TC) {
		m_s.attch_status_reg &= ~HDD_ASR_DATA_REQ;
		unsigned c = m_s.cur_cylinder;
		cmd_timer();
		if(TC) { // Terminal Count line, done
			PDEBUGF(LOG_V2, LOG_HDD, "<<DMA READ TC>> C:%d,H:%d,S:%d,nS:%d\n",
					m_s.cur_cylinder, m_s.cur_head,
					m_s.cur_sector, m_s.ccb.num_sectors);
		} else {
			m_s.attch_status_reg |= HDD_ASR_DATA_REQ;
			uint32_t time = m_sec_tx_us;
			if(c != m_s.cur_cylinder) {
				time += m_trk2trk_us;
			}
			g_machine.activate_timer(m_dma_timer, HDD_TIMING?time:HDD_DEFTIME_US, 0);
		}
		g_dma.set_DRQ(HDD_DMA_CHAN, 0);
	}
	return len;
}

void HardDrive::dma_timer()
{
	g_dma.set_DRQ(HDD_DMA_CHAN, 1);
	g_machine.deactivate_timer(m_dma_timer);
}

void HardDrive::cmd_timer()
{
	if(m_s.attention_reg & HDD_ATT_CCB) {
		ASSERT(m_s.ccb.command>=0 && m_s.ccb.command<=0xF);
		m_s.ssb.clear();
		ms_cmd_funcs[m_s.ccb.command](*this);
		m_s.ssb.valid = true; //command functions update the SSB so it's valid
		PDEBUGF(LOG_V2, LOG_HDD, "command exec end: C:%d,H:%d,S:%d,nS:%d\n",
			m_s.cur_cylinder, m_s.cur_head, m_s.cur_sector, m_s.ccb.num_sectors);
	} else if(m_s.attention_reg & HDD_ATT_CSB) {
		PERRF_ABORT(LOG_HDD, "CSB not implemented\n");
	} else {
		m_s.int_status_reg |= HDD_ISR_CMD_REJECT;
		PERRF_ABORT(LOG_HDD, "invalid attention request\n");
	}

	g_machine.deactivate_timer(m_cmd_timer);
}

void HardDrive::set_cur_sector(unsigned _h, unsigned _s)
{
	m_s.cur_head = _h;
	if(_h >= m_disk->heads) {
		PDEBUGF(LOG_V2, LOG_HDD, "seek: head %d >= %d\n", _h, m_disk->heads);
		m_s.cur_head %= m_disk->heads;
	}

	//warning: sectors are 1-based
	if(_s > 0) {
		if(_s > m_disk->spt) {
			PDEBUGF(LOG_V2, LOG_HDD, "seek: sector %d > %d\n", _s, m_disk->spt);
			m_s.cur_sector = (_s - 1)%m_disk->spt + 1;
		} else {
			m_s.cur_sector = _s;
		}
	}
}

bool HardDrive::seek(unsigned _c)
{
	if(_c >= m_disk->cylinders) {
		//TODO is it a temination error?
		//what about command reject and ERP invoked?
		m_s.int_status_reg |= HDD_ISR_TERMINATION;
		m_s.ssb.cylinder_err = true;
		PDEBUGF(LOG_V2, LOG_HDD, "seek error: cyl=%d > %d\n", _c, m_disk->cylinders);
		return false;
	}
	m_s.eoc = false;
	m_s.cur_cylinder = _c;

	return true;
}

void HardDrive::increment_sector()
{
	m_s.cur_sector++;
	//warning: sectors are 1-based
	if(m_s.cur_sector > m_disk->spt) {
		m_s.cur_sector = 1;
		m_s.cur_head++;
		if(m_s.cur_head >= m_disk->heads) {
			m_s.cur_head = 0;
			m_s.cur_cylinder++;
		}

		if(m_s.cur_cylinder >= m_disk->cylinders) {
			m_s.cur_cylinder = m_disk->cylinders;
			m_s.eoc = true;
			PDEBUGF(LOG_V2, LOG_HDD, "increment_sector: clamping cylinder to max\n");
		}
	}
}

void HardDrive::read_sector(unsigned _c, unsigned _h, unsigned _s)
{
	PDEBUGF(LOG_V2, LOG_HDD, "SECTOR READ\n");

	unsigned lba = chs_to_lba(_c,_h,_s);
	ASSERT(lba < m_sectors);
	int64_t offset = lba*512;
	int64_t pos = m_disk->lseek(offset, SEEK_SET);
	ASSERT(pos == offset);
	ssize_t res = m_disk->read(m_s.data_stack, 512);
	ASSERT(res == 512);
}

void HardDrive::write_sector(unsigned _c, unsigned _h, unsigned _s)
{
	PDEBUGF(LOG_V2, LOG_HDD, "SECTOR WRITE\n");

	unsigned lba = chs_to_lba(_c,_h,_s);
	ASSERT(lba < m_sectors);
	int64_t offset = lba*512;
	int64_t pos = m_disk->lseek(offset, SEEK_SET);
	ASSERT(pos == offset);
	ssize_t res = m_disk->write(m_s.data_stack, 512);
	ASSERT(res == 512);
}

void HardDrive::cylinder_error()
{
	m_s.int_status_reg |= HDD_ISR_TERMINATION;
	m_s.ssb.cylinder_err = true;
	PDEBUGF(LOG_V2, LOG_HDD, "error: cyl > %d\n", m_disk->cylinders);
}

void HardDrive::read_data_cmd()
{
	if(m_s.ccb.auto_seek) {
		if(!seek(m_s.ccb.cylinder)) {
			/* When the CCB specifies a cylinder beyond the limit, no step
			 * operation is done and the heads do not move.
			 */
			raise_interrupt();
			return;
		}
		m_s.ccb.auto_seek = false;
	}
	ASSERT(m_s.ccb.num_sectors>0);
	ASSERT(!m_s.eoc);
	if(m_s.eoc) {
		cylinder_error();
		raise_interrupt();
		return;
	}
	//read the sector;
	read_sector(m_s.cur_cylinder, m_s.cur_head, m_s.cur_sector);
	/////
	m_s.data_ptr = 0;
	m_s.data_size = 512;
	m_s.attch_status_reg |= HDD_ASR_DATA_REQ;
	m_s.attch_status_reg |= HDD_ASR_DIR;
	m_s.attch_status_reg &= ~HDD_ASR_BUSY;
	m_s.ccb.num_sectors--;

	uint32_t time = m_exec_time_us;
	if(m_s.ccb.num_sectors == 0) {
		m_s.attention_reg &= ~HDD_ATT_CCB;
	} else {
		unsigned s = m_s.cur_cylinder;
		increment_sector();
		if(s != m_s.cur_cylinder) {
			time += m_trk2trk_us;
		}
	}

	if(m_s.attch_ctrl_reg & HDD_ACR_DMA_EN) {
		g_machine.activate_timer(m_dma_timer, HDD_TIMING?time:HDD_DEFTIME_US, 0);
	} else {
		raise_interrupt();
	}
}

void HardDrive::read_check_cmd()
{
	m_s.attention_reg &= ~HDD_ATT_CCB;
	m_s.attch_status_reg &= ~HDD_ASR_BUSY;
	raise_interrupt();

	if(m_s.ccb.auto_seek) {
		if(!seek(m_s.ccb.cylinder)) {
			return;
		}
	}
	while(m_s.ccb.num_sectors>0) {
		if(m_s.eoc) {
			cylinder_error();
			return;
		}
		//nothing to do, data checks are always successful
		m_s.ccb.num_sectors--;
		if(m_s.ccb.num_sectors > 0) {
			increment_sector();
		}
	}
}

void HardDrive::read_ext_cmd()
{
	PERRF_ABORT(LOG_HDD, "READ_EXT: command not implemented\n");
}

void HardDrive::read_id_cmd()
{
	PERRF_ABORT(LOG_HDD, "READ_ID: command not implemented\n");
}

void HardDrive::recalibrate_cmd()
{
	PERRF_ABORT(LOG_HDD, "RECALIBRATE: command not implemented\n");
}

void HardDrive::write_data_cmd()
{
	if(m_s.ccb.auto_seek) {
		if(!seek(m_s.ccb.cylinder)) {
			/* When the CCB specifies a cylinder beyond the limit, no step
			 * operation is done and the heads do not move.
			 */
			raise_interrupt();
			return;
		}
		//set_cur_sector(m_s.ccb.head, m_s.ccb.sector);
		m_s.ccb.auto_seek = false;
	}
	if(!(m_s.attch_status_reg & HDD_ASR_DATA_REQ)) {
		ASSERT(m_s.data_size == 512);
		ASSERT(m_s.data_ptr == 512);
		ASSERT(m_s.ccb.num_sectors>0);

		if(m_s.eoc) {
			cylinder_error();
			raise_interrupt();
			return;
		}
		//write the sector;
		write_sector(m_s.cur_cylinder, m_s.cur_head, m_s.cur_sector);
		/////
		m_s.ccb.num_sectors--;
		m_s.data_ptr = 0;
		if(m_s.ccb.num_sectors == 0) {
			m_s.data_size = 0;
			m_s.attention_reg &= ~HDD_ATT_CCB;
			raise_interrupt();
		} else {
			increment_sector();
			m_s.attch_status_reg |= HDD_ASR_DATA_REQ;
			m_s.data_size = 512;
		}
	} else {
		m_s.attch_status_reg &= ~HDD_ASR_BUSY;
		raise_interrupt();
	}
}

void HardDrive::write_vfy_cmd()
{
	PERRF_ABORT(LOG_HDD, "WRITE_VFY: command not implemented\n");
}

void HardDrive::write_ext_cmd()
{
	PERRF_ABORT(LOG_HDD, "WRITE_EXT: command not implemented\n");
}

void HardDrive::format_disk_cmd()
{
	PERRF_ABORT(LOG_HDD, "FORMAT_DISK: command not implemented\n");
}

void HardDrive::seek_cmd()
{
	if(m_s.ccb.park) {
		//not really a park...
		seek(0);
	} else {
		seek(m_s.ccb.cylinder);
	}
	m_s.attention_reg &= ~HDD_ATT_CCB;
	m_s.attch_status_reg &= ~HDD_ASR_BUSY;
	raise_interrupt();
}

void HardDrive::format_trk_cmd()
{
	PERRF_ABORT(LOG_HDD, "FORMAT_TRK: command not implemented\n");
}

void HardDrive::undefined_cmd()
{
	PERRF_ABORT(LOG_HDD, "unknown command!\n");
}

