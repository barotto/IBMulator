// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's formats/flopimg.cpp

#include "ibmulator.h"
#include "floppyfmt.h"
#include "floppydisk.h"
#include "filesys.h"


const std::map<FloppyDisk::StdType, FloppyDisk::Properties> FloppyDisk::std_types = {
             //type     trk  s  spt  ssize  secs   capacity   drate       wprot  str
  { FD_NONE, { FD_NONE,   0, 0,   0,     0,    0,         0,  DRATE_250,  false, "none"           }},
  { DD_160K, { DD_160K,  40, 1,   8,   512,  320,  160*1024,  DRATE_250,  false, "5.25\" DD 160K" }},
  { DD_180K, { DD_180K,  40, 1,   9,   512,  360,  180*1024,  DRATE_250,  false, "5.25\" DD 180K" }},
  { DD_320K, { DD_320K,  40, 2,   8,   512,  640,  320*1024,  DRATE_250,  false, "5.25\" DD 320K" }},
  { DD_360K, { DD_360K,  40, 2,   9,   512,  720,  360*1024,  DRATE_250,  false, "5.25\" DD 360K" }},
// raw images 5.25 QD cannot be distinguished from 3.5 DD 
// with 3.5 DD images mounted as 5.25 QD, DOS/BIOS incorrectly uses double stepping
//{ QD_720K, { QD_720K,  80, 2,   9,   512, 1440,  720*1024,  DRATE_250,  false, "5.25\" QD 720K" }},
  { DD_720K, { DD_720K,  80, 2,   9,   512, 1440,  720*1024,  DRATE_250,  false, "3.5\" DD 720K"  }},
  { HD_1_20, { HD_1_20,  80, 2,  15,   512, 2400, 1200*1024,  DRATE_500,  false, "5.25\" HD 1.2M" }},
  { HD_1_44, { HD_1_44,  80, 2,  18,   512, 2880, 1440*1024,  DRATE_500,  false, "3.5\" HD 1.44M" }},
  { HD_1_68, { HD_1_68,  80, 2,  21,   512, 3360, 1680*1024,  DRATE_500,  false, "3.5\" HD 1.68M" }},
  { HD_1_72, { HD_1_72,  82, 2,  21,   512, 3444, 1722*1024,  DRATE_500,  false, "3.5\" HD 1.72M" }},
  { ED_2_88, { ED_2_88,  80, 2,  36,   512, 5760, 2880*1024,  DRATE_1000, false, "3.5\" ED 2.88M" }}
};


FloppyDisk::FloppyDisk(const Properties &_props)
{
	m_props = _props;

	track_array.resize(m_props.tracks);

	for(unsigned i=0; i<track_array.size(); i++) {
		track_array[i].resize(m_props.sides);
	}
}

FloppyDisk::~FloppyDisk()
{
}

bool FloppyDisk::load(std::string _path, std::shared_ptr<FloppyFmt> _format)
{
	if(!_format) {
		return false;
	}
	m_loaded_image = "";
	m_format = nullptr;

	std::ifstream fstream = FileSys::make_ifstream(_path.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PERRF(LOG_GUI, "Cannot open file '%s' for reading\n", _path.c_str());
		return false;
	}

	if(_format->load(fstream, *this)) {
		m_format = _format;
		m_loaded_image = _path;
		return true;
	}

	return false;
}

bool FloppyDisk::save(std::string _path, std::shared_ptr<FloppyFmt> _format)
{
	if(!_format) {
		return false;
	}

	std::string dir, base, ext;
	if(!FileSys::get_path_parts(_path.c_str(), dir, base, ext)) {
		PERRF(LOG_GUI, "Destination path '%s' is not valid\n", _path.c_str());
		return false;
	}

	auto tmp = FileSys::get_next_filename_time(_path.c_str());
	if(tmp.empty()) {
		PERRF(LOG_GUI, "Cannot write '%s'\n", _path.c_str());
		return false;
	}

	std::ofstream fstream = FileSys::make_ofstream(tmp.c_str(), std::ios::binary);
	if(!fstream.is_open()) {
		PERRF(LOG_GUI, "Cannot write into directory '%s'\n", dir.c_str());
		return false;
	}

	m_dirty = !_format->save(fstream, *this);

	fstream.close();

	if(!m_dirty) {
		if(FileSys::file_exists(_path.c_str())) {
			if(FileSys::is_file_writeable(_path.c_str())) {
				if(FileSys::remove(_path.c_str()) < 0) {
					PERRF(LOG_GUI, "Cannot overwrite '%s', creating a copy...\n", _path.c_str());
				} else if(FileSys::rename_file(tmp.c_str(), _path.c_str()) < 0) {
					PERRF(LOG_GUI, "Error renaming '%s'\n", tmp.c_str());
				}
			} else {
				PERRF(LOG_GUI, "Cannot overwrite '%s', creating a copy...\n", _path.c_str());
			}
		} else if(FileSys::rename_file(tmp.c_str(), _path.c_str()) < 0) {
			PERRF(LOG_GUI, "Error renaming '%s'\n", tmp.c_str());
		}
	} else {
		PERRF(LOG_GUI, "Cannot save '%s'\n", _path.c_str());
		if(FileSys::remove(tmp.c_str()) < 0) {
			PWARNF(LOG_V0, LOG_GUI, "Cannot remove '%s'\n", tmp.c_str());
		}
	}

	m_dirty_restore = m_dirty;

	return !m_dirty;
}

void FloppyDisk::load_state(std::string _imgpath, std::string _binpath)
{
	std::ifstream fstream = FileSys::make_ifstream(_binpath.c_str(), std::ios::binary);

	if(!fstream.is_open()){
		PERRF(LOG_FDC, "Cannot open file '%s' for reading\n", _binpath.c_str());
		throw std::exception();
	}

	m_loaded_image = _imgpath;
	m_format.reset(FloppyFmt::find(_imgpath));

	// dirty condition
	// (dirty_restore condition is not saved)
	fstream.read(reinterpret_cast<char*>(&m_dirty), sizeof m_dirty);

	// track data
	fstream.exceptions(std::ifstream::badbit);
	for(int track=0; track < m_props.tracks; track++) {
		for(int head=0; head < m_props.sides; head++) {
			track_array[track][head].load_state(fstream);
		}
	}

	// caller should set write protected state

	m_dirty_restore = false;
}

void FloppyDisk::save_state(std::string _binpath)
{
	std::ofstream fstream = FileSys::make_ofstream(_binpath.c_str(), std::ios::binary);
	if(!fstream.is_open()){
		PERRF(LOG_GUI, "Cannot open file '%s' for writing\n", _binpath.c_str());
		throw std::exception();
	}
	fstream.exceptions(std::ifstream::badbit);

	// dirty condition
	fstream.write(reinterpret_cast<char*>(&m_dirty), sizeof m_dirty);

	// track data
	for(int track=0; track < m_props.tracks; track++) {
		for(int head=0; head < m_props.sides; head++) {
			track_array[track][head].save_state(fstream);
		}
	}
}

bool FloppyDisk::can_be_committed() const
{
	if(!m_format) {
		PDEBUGF(LOG_V0, LOG_FDC, "Missing image format!\n");
		return false;
	}
	if(!m_format->can_save()) {
		PWARNF(LOG_V0, LOG_FDC, "Format %s doesn't support save\n", m_format->name());
	}
	return m_format->can_save();
}

void FloppyDisk::track_info::save_state(std::ofstream &_file)
{
	auto size = cell_data.size();
	_file.write(reinterpret_cast<char const*>(&size), sizeof(size));
	_file.write(reinterpret_cast<char const*>(cell_data.data()), cell_data.size() * 4);
	_file.write(reinterpret_cast<char const*>(&write_splice), sizeof(write_splice));
	_file.write(reinterpret_cast<char const*>(&has_damaged_cells), sizeof(has_damaged_cells));
}

void FloppyDisk::track_info::load_state(std::ifstream &_file)
{
	decltype(cell_data.size()) size;
	_file.read(reinterpret_cast<char*>(&size), sizeof(size));
	cell_data.resize(size);
	_file.read(reinterpret_cast<char*>(cell_data.data()), cell_data.size() * 4);
	_file.read(reinterpret_cast<char*>(&write_splice), sizeof(write_splice));
	_file.read(reinterpret_cast<char*>(&has_damaged_cells), sizeof(has_damaged_cells));
}

void FloppyDisk::get_maximal_geometry(int &_tracks, int &_heads) const
{
	_tracks = track_array.size();
	_heads = m_props.sides;
}

void FloppyDisk::get_actual_geometry(int &_tracks, int &_heads) const
{
	int maxt = track_array.size() - 1;
	int maxh = m_props.sides - 1;

	while(maxt >= 0) {
		for(int i=0; i<=maxh; i++) {
			if(!track_array[maxt][i].cell_data.empty()) {
				goto track_done;
			}
		}
		maxt--;
	}
	track_done:

	if(maxt >= 0) {
		while(maxh >= 0) {
			for(int i=0; i<=maxt; i++) {
				if(!track_array[i][maxh].cell_data.empty()) {
					goto head_done;
				}
			}
			maxh--;
		}
	} else {
		maxh = -1;
	}
	head_done:

	_tracks = maxt + 1;
	_heads = maxh + 1;
}

bool FloppyDisk::track_is_formatted(int track, int head)
{
	if(int(track_array.size()) <= track) {
		return false;
	}
	if(int(track_array[track].size()) <= head) {
		return false;
	}
	const auto &data = track_array[track][head].cell_data;
	if(data.empty()) {
		return false;
	}
	for(uint32_t mg : data) {
		if((mg & FloppyDisk::MG_MASK) == FloppyDisk::MG_F) {
			return true;
		}
	}
	return false;
}

void FloppyDisk::resize_tracks(unsigned _num_of_tracks)
{
	track_array.resize(_num_of_tracks);

	if(m_props.tracks < _num_of_tracks) {
		for(unsigned i=m_props.tracks; i<_num_of_tracks; i++) {
			track_array[i].resize(m_props.sides);
		}
	}
	
	m_props.tracks = _num_of_tracks;
}

void FloppyDisk::read_sector(uint8_t _c, uint8_t _h, uint8_t _s, uint8_t *buffer, uint32_t bytes)
{
	// TODO?
	UNUSED(_c);
	UNUSED(_h);
	UNUSED(_s);
	UNUSED(buffer);
	UNUSED(bytes);

	PDEBUGF(LOG_V0, LOG_FDC, "read_sector not implemented for flux-based disks\n");
}

void FloppyDisk::write_sector(uint8_t _c, uint8_t _h, uint8_t _s, const uint8_t *buffer, uint32_t bytes)
{
	// TODO?
	UNUSED(_c);
	UNUSED(_h);
	UNUSED(_s);
	UNUSED(buffer);
	UNUSED(bytes);

	PDEBUGF(LOG_V0, LOG_FDC, "write_sector not implemented for flux-based disks\n");
}

FloppyDisk::Properties FloppyDisk::find_std_type(unsigned _variant)
{
	if(_variant & TYPE_MASK) {
		auto it = std_types.find(StdType(_variant));
		if(it != std_types.end()) {
			return it->second;
		}
	}
	return {FD_NONE};
}