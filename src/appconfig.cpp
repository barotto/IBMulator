/*
 * Copyright (C) 2015, 2016  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "filesys.h"
#include "appconfig.h"
#include "program.h"
#include <algorithm>
#include <regex>
#include <cctype>
#include <cstdlib>
#include "ini/ini.h"

using std::string;

ini_file_t AppConfig::ms_def_values = {
	{ PROGRAM_SECTION, {
		{ PROGRAM_MEDIA_DIR, "" },
		{ PROGRAM_CAPTURE_DIR, "" }
	} },

	{ CPU_SECTION, {
		{ CPU_FREQUENCY, "10.0"}
	} },

	{ GUI_SECTION, {
		{ GUI_KEYMAP, "keymaps/pc-us.map"},
		{ GUI_MOUSE_TYPE, "ps2" },
		{ GUI_MOUSE_GRAB, "yes" },
		{ GUI_MOUSE_ACCELERATION, "no" },
		{ GUI_GRAB_METHOD, "MOUSE3" }, //CTRL-F10 or MOUSE3
		{ GUI_SCREEN_DPI, "96" },
		{ GUI_WIDTH, "640" },
		{ GUI_HEIGHT, "480" },
		{ GUI_FULLSCREEN, "no" },
		{ GUI_SHOW_LEDS, "no" },
		{ GUI_MODE, "normal" },
		{ GUI_BG_R, "59" },
		{ GUI_BG_G, "82" },
		{ GUI_BG_B, "98" }
	} },

	{ DISPLAY_SECTION, {
		{ DISPLAY_NORMAL_ASPECT, "original" },
		{ DISPLAY_NORMAL_SHADER, "gui/shaders/fb-normal.fs" },
		{ DISPLAY_NORMAL_FILTER, "bilinear" },
		{ DISPLAY_REALISTIC_SHADER, "gui/shaders/fb-realistic.fs" },
		{ DISPLAY_REALISTIC_FILTER, "bicubic" },
		{ DISPLAY_REALISTIC_SCALE, "1.0" },
		{ DISPLAY_REALISTIC_AMBIENT, "0.6" },
		{ DISPLAY_BRIGHTNESS, "1.0" },
		{ DISPLAY_CONTRAST, "1.0" },
		{ DISPLAY_SATURATION, "1.0" }
	} },

	{ CMOS_SECTION, {
		{ CMOS_IMAGE_FILE, "cmos.bin" },
		{ CMOS_IMAGE_RTC_INIT, "no" },
		{ CMOS_IMAGE_SAVE, "yes" }
	} },

	{ MEM_SECTION, {
		{ MEM_BASE_SIZE, "640" },
		{ MEM_EXT_SIZE, "384" },
		{ MEM_ROMSET, "PS1_2011_ROM.zip" }
	} },

	{ DRIVES_SECTION, {
		{ DRIVES_FDD_A, "3.5" },
		{ DRIVES_FDD_B, "none" },
		{ DRIVES_HDD, "35" }
	} },

	{ DISK_A_SECTION, {
		{ DISK_TYPE, "1.44M" },
		{ DISK_INSERTED, "no" },
		{ DISK_READONLY, "no" },
		{ DISK_PATH, "" }
	} },

	{ DISK_B_SECTION, {
		{ DISK_TYPE, "1.44M" },
		{ DISK_INSERTED, "no" },
		{ DISK_READONLY, "no" },
		{ DISK_PATH, "" }
	} },

	{ DISK_C_SECTION, {
		{ DISK_READONLY,   "no" },
		{ DISK_SAVE,       "yes" },
		{ DISK_PATH,       "hdd.img" },
		{ DISK_CYLINDERS,  "921" },
		{ DISK_HEADS,      "2" },
		{ DISK_SPT,        "33" },
		{ DISK_SEEK_MAX,   "40.0" },
		{ DISK_SEEK_TRK,   "8.0" },
		{ DISK_ROT_SPEED,  "3600" },
		{ DISK_INTERLEAVE, "4" },
		{ DISK_OVERH_TIME, "5.0" }
	} },

	{ MIXER_SECTION, {
		{ MIXER_RATE, "44100" },
		{ MIXER_SAMPLES, "1024" },
		{ MIXER_PREBUFFER, "50" },
		{ MIXER_VOLUME, "1.0" },
		{ MIXER_PCSPEAKER, "yes" },
		{ MIXER_PS1AUDIO, "yes" }
	} },

	{ SOUNDFX_SECTION, {
		{ SOUNDFX_VOLUME,   "1.0" },
		{ SOUNDFX_FDD_SPIN, "0.4" },
		{ SOUNDFX_FDD_SEEK, "0.4" },
		{ SOUNDFX_HDD_SPIN, "0.4" },
		{ SOUNDFX_HDD_SEEK, "0.3" },
		{ SOUNDFX_SYSTEM,   "1.0" }
	} },

	{ COM_SECTION, {
		{ COM_ENABLED, "yes" },
		{ COM_MODE, "null" },
		{ COM_DEV, "" }
	} },

	{ LPT_SECTION, {
		{ LPT_ENABLED, "yes" },
		{ LPT_PORT, "LPT1" },
		{ LPT_FILE, "" }
	} },
};

ini_filehelp_t AppConfig::ms_help = {
		{ "HEADER",
"; This is the configuration file for " PACKAGE_STRING "\n"
"; Lines starting with a ; are comment lines and are ignored. "
" They are used to document the effect of each option.\n"
"; Paths can be absolute or relative.\n"
"; Relative paths are searched in this order:\n"
"; 1. the media directory (in case of floppy/hdd images)\n"
"; 2. the user directory (the folder where this file is normally located)\n"
"; 3. the program's assets directory\n"
		},

		{ PROGRAM_SECTION,
";   media_dir: The default directory used to search for floppy and hdd images.\n"
"; capture_dir: Directory where things like wave files, savestates and screenshots get captured.\n"
		},

		{ CPU_SECTION,
"; frequency: The CPU frequency in MHz.\n"
		},

		{ GUI_SECTION,
";        mode: Possible values: normal, compact, realistic.\n"
";                  normal: the system unit places itself at the bottom of the display and is always visible\n"
";                 compact: the system unit disappears when input is grabbed or CTRL-F1 is pressed\n"
";               realistic: the system is rendered in its entirety, monitor included\n"
";      keymap: Keymap table file. The file format is taken from Bochs, with some differences.\n"
";              Open a .map file to read comments on how to edit it.\n"
";        grab: If 'no' then the mouse will not be hidden when grabbed (useful when debugging IBMulator)\n"
"; grab_method: Method to use for mouse grabbing\n"
";              Possible values: MOUSE3, CTRL-F10\n"
";       mouse: Mouse type.\n"
";              Possible values: none, ps2, serial\n"
"; mouse_accel: Enable mouse acceleration\n"
";       width: window width in pixel.\n"
";      height: window height in pixel (for normal GUI mode it doesn't include the system unit.)\n"
";  fullscreen: Start directly in fullscreen. (Press ALT-Enter to go back)\n"
";         dpi: Resolution of the host display in DPI (currently used only for mouse acceleration).\n"
";      bg_XXX: Background window color\n"
";   show_leds: Show or hide the drives motor activity led at the bottom-right (useful in compact mode)\n"
		},

		{ DISPLAY_SECTION,
"; The following parameters are used for normal and compact GUI modes only:\n"
";    normal_aspect: VGA aspect ratio\n"
";                   Possible values: original, adaptive, scaled.\n"
";                    original: 4:3 aspect ratio\n"
";                    adaptive: screen will be scaled maintaining the aspect ratio of the current video mode\n"
";                      scaled: screen will be scaled to fill your monitor\n"
";    normal_shader: GLSL fragment shader to use for VGA rendering\n"
";    normal_filter: VGA scaling filter\n"
";                   Possible values: nearest, bilinear, bicubic\n"
"; The following parameters are used for realistic GUI mode only:\n"
"; realistic_shader: GLSL fragment shader to use for VGA rendering\n"
"; realistic_filter: VGA scaling filter\n"
";                   Possible values: nearest, bilinear, bicubic\n"
";  realistic_scale: VGA dimensions as a scaling factor. Use this to adjust the image size.\n"
";                   1.0 is the original VGA image size and ~1.2 fills the screen.\n"
";realistic_ambient: Intensity of the ambient light. It is a weight for the monitor reflection map.\n"
";                   Use a number between 0.0 and 1.0. 0.0 gives a pitch-black monitor.\n"
"; The following parameters are used for any GUI mode:\n"
";       brightness: Monitor brightness.\n"
";                   When in realistic GUI mode it's clamped to 1.3\n"
";         contrast: Monitor contrast.\n"
";                   When in realistic GUI mode it's clamped to 1.3\n"
";       saturation: Monitor saturation.\n"
		},

		{ CMOS_SECTION, ""
";      image: Path of the binary file to use for the CMOS initialisation values.\n"
"; image_init: Yes if you want to initialise the RTC with the values in the CMOS image\n"
"; image_save: Yes if you want to save the CMOS in the image file when the machine is powered off\n"
		},

		{ MEM_SECTION,
";   romset: Path to a bin/zip file or directory containing the ROM set to use (for the correct format see the README)\n"
";     base: Size of the base RAM in KiB\n"
"; extended: Size of the extended RAM in KiB\n"
		},

		{ DRIVES_SECTION,
"; floppy_a: The type of floppy drive A.\n"
";           Possible values: none, 3.5, 5.25\n"
"; floppy_b: The type of floppy drive B.\n"
";           Possible values: none, 3.5, 5.25\n"
";      hdd: The type of fixed disk drive C.\n"
";           Possible values: any number between 0 and 45 (15 excluded)\n"
";                0: no disk installed\n"
";               15: reserved, don't use it\n"
";               35: the original WDL-330P 30MB disk drive\n"
";             1-44: standard type (see the project page for the list of types supported by the BIOS)\n"
";               45: custom type (specify the geometry in the hdd section)\n"
		},

		{ DISK_A_SECTION,
"; These options are used to mount a floppy at program launch.\n"
";     path: Path of a floppy image file to mount at program lauch\n"
"; inserted: Yes if the floppy is inserted at program lauch\n"
"; readonly: Yes if the floppy image should be write protected\n"
";     type: The type of the inserted floppy.\n"
";           Possible values: none, 1.44M, 720K, 1.2M, 360K\n"
		},

		{ DISK_B_SECTION,
"; These options are used to mount a floppy at program launch.\n"
";     path: Path of a floppy image file to mount at program lauch\n"
"; inserted: Yes if the floppy is inserted at program lauch\n"
"; readonly: Yes if the floppy image should be write protected\n"
";     type: The type of the inserted floppy.\n"
";           Possible values: none, 1.44M, 720K, 1.2M, 360K\n"
		},

		{ DISK_C_SECTION,
";     path: Path of the image file to mount\n"
"; readonly: Yes if the disk image should be write protected (a temporary image will be used)\n"
";     save: When you restore a savestate the disk is restored as well, as a temporary read-write image.\n"
";           Set this option to 'yes' if you want to make the changes permanent at machine power off in the file specified at 'path' "
"(unless it is write-protected)\n"
"; The following parameters are used for disk type 45 (custom type):\n"
";   cylinders: Number of cylinders (max. 1024)\n"
";       heads: Number of heads (max. 16)\n"
";     sectors: Number of sectors per track (max. 62)\n"
"; Drive capacity is cylinders*heads*sectors*512, for a maximum of 496MiB.\n"
"; The following performance parameters are used for any disk type except 35 and 38:\n"
";    seek_max: Maximum seek time in milliseconds\n"
";    seek_trk: Track-to-track seek time in milliseconds\n"
";   rot_speed: Rotational speed in RPM\n"
";  interleave: Interleave ratio\n"
";  overh_time: Controller overhead time in milliseconds\n"
		},

		{ MIXER_SECTION,
"; prebuffer: How many milliseconds of data to prebuffer before audio start to be emitted.\n"
";   samples: Audio samples buffer size; a larger buffer might help sound stuttering.\n"
";            Possible values: 1024, 2048, 4096, 8192, 512, 256.\n"
";      rate: Sample rate.\n"
";            Possible values: 48000, 44100, 32000, 22050.\n"
";    volume: Audio volume of the sound cards.\n"
";            Possible values: any positive real number. When in realistic GUI mode it's clamped to 1.3\n"
"; pcspeaker: Enable PC-Speaker emulation.\n"
";  ps1audio: Enable PS/1 Audio Card emulation.\n"
		},

		{ SOUNDFX_SECTION,
"; Volumes are expressed as positive real numbers.\n"
";   volume: Audio volume of the sound effects. Set to 0.0 to disable, 1.0 for normal.\n"
"; fdd_seek: Volume of FDD seeks.\n"
"; fdd_spin: Volume of FDD spin noise.\n"
"; hdd_seek: Volume of HDD seeks.\n"
"; hdd_spin: Volume of HDD spin noise.\n"
";   system: Volume of system unit's and monitor's noises.\n"
		},

		{ COM_SECTION, "" },

		{ LPT_SECTION, "" },
};

std::vector<std::pair<std::string, std::vector<std::string>>> AppConfig::ms_keys_order = {
	{ PROGRAM_SECTION, {
		PROGRAM_MEDIA_DIR,
		PROGRAM_CAPTURE_DIR
	} },
	{ GUI_SECTION, {
		GUI_MODE,
		GUI_KEYMAP,
		GUI_MOUSE_GRAB,
		GUI_GRAB_METHOD,
		GUI_MOUSE_TYPE,
		GUI_MOUSE_ACCELERATION,
		GUI_WIDTH,
		GUI_HEIGHT,
		GUI_FULLSCREEN,
		GUI_SCREEN_DPI,
		GUI_BG_R,
		GUI_BG_G,
		GUI_BG_B,
		GUI_SHOW_LEDS
	} },
	{ DISPLAY_SECTION, {
		DISPLAY_NORMAL_ASPECT,
		DISPLAY_NORMAL_SHADER,
		DISPLAY_NORMAL_FILTER,
		DISPLAY_REALISTIC_SHADER,
		DISPLAY_REALISTIC_FILTER,
		DISPLAY_REALISTIC_SCALE,
		DISPLAY_REALISTIC_AMBIENT,
		DISPLAY_BRIGHTNESS,
		DISPLAY_CONTRAST,
		DISPLAY_SATURATION
	} },
	{ CPU_SECTION, {
		CPU_FREQUENCY
	} },
	{ MEM_SECTION, {
		MEM_ROMSET,
		MEM_BASE_SIZE,
		MEM_EXT_SIZE
	} },
	{ CMOS_SECTION, {
		CMOS_IMAGE_FILE,
		CMOS_IMAGE_RTC_INIT,
		CMOS_IMAGE_SAVE
	} },
	{ DRIVES_SECTION, {
		DRIVES_FDD_A,
		DRIVES_FDD_B,
		DRIVES_HDD
	} },
	{ DISK_A_SECTION, {
		DISK_PATH,
		DISK_INSERTED,
		DISK_READONLY,
		DISK_TYPE
	} },
	{ DISK_B_SECTION, {
		DISK_PATH,
		DISK_INSERTED,
		DISK_READONLY,
		DISK_TYPE
	} },
	{ DISK_C_SECTION, {
		DISK_PATH,
		DISK_READONLY,
		DISK_SAVE,
		DISK_CYLINDERS,
		DISK_HEADS,
		DISK_SPT,
		DISK_SEEK_MAX,
		DISK_SEEK_TRK,
		DISK_ROT_SPEED,
		DISK_INTERLEAVE,
		DISK_OVERH_TIME
	} },
	{ MIXER_SECTION, {
		MIXER_PREBUFFER,
		MIXER_SAMPLES,
		MIXER_RATE,
		MIXER_VOLUME,
		MIXER_PCSPEAKER,
		MIXER_PS1AUDIO
	} },
	{ SOUNDFX_SECTION, {
		SOUNDFX_VOLUME,
		SOUNDFX_FDD_SPIN,
		SOUNDFX_FDD_SEEK,
		SOUNDFX_HDD_SPIN,
		SOUNDFX_HDD_SEEK,
		SOUNDFX_SYSTEM
	} },
	{ COM_SECTION, {
		COM_ENABLED,
		COM_MODE,
		COM_DEV
	} },
	{ LPT_SECTION, {
		LPT_ENABLED,
		LPT_PORT,
		LPT_FILE
	} }
};

AppConfig::AppConfig()
{

}

void AppConfig::reset()
{
	m_values.clear();
}

void AppConfig::merge(AppConfig &_config)
{
	//deep merge
	for(auto section : _config.m_values) {
		//is the current section already present?
		if(m_values.count(section.first)) {
			//if so, merge the 2 sections
			for(auto secval : section.second) {
				m_values[section.first][secval.first] = secval.second;
			}
		} else {
			//otherwise just copy it
			m_values[section.first] = section.second;
		}
	}
}

long AppConfig::parse_int(const string &_str)
{
	const char* value = _str.c_str();
	char* end;
	// This parses "1234" (decimal) and also "0x4D2" (hex)
	long n = strtol(value, &end, 0);
	if(end <= value) {
		PWARNF(LOG_PROGRAM, "'%s' is not a valid integer\n", value);
		throw std::exception();
	}
	return n;
}

double AppConfig::parse_real(const string &_str)
{
	const char* value = _str.c_str();
	char* end;
	double n = strtod(value, &end);
	if(end <= value) {
		PWARNF(LOG_PROGRAM, "'%s' is not a valid real\n", value);
		throw std::exception();
	}
	return n;
}

bool AppConfig::parse_bool(string _str)
{
	// Convert to lower case to make string comparisons case-insensitive
	std::transform(_str.begin(), _str.end(), _str.begin(), ::tolower);
	if (_str == "true" || _str == "yes" || _str == "on" || _str == "1") {
		return true;
	} else if (_str == "false" || _str == "no" || _str == "off" || _str == "0") {
		return false;
	} else {
		PWARNF(LOG_PROGRAM, "'%s' is not a valid bool\n", _str.c_str());
		throw std::exception();
	}
}

int AppConfig::get_error()
{
	return m_error;
}

void AppConfig::parse(const string &_filename)
{
	m_error = ini_parse(_filename.c_str(), value_handler, this);
	if(m_error != 0) {
		throw std::exception();
	}
	m_parsed_file = _filename;
}

string AppConfig::get(ini_file_t &_values, const string &section, const string &name)
{
	string s = make_key(section);
	string n = make_key(name);
	string value;
	if(_values.count(s)) {
		ini_section_t sec = _values[s];
		if(sec.count(n)) {
			value = sec[n];
		} else {
			PDEBUGF(LOG_V2, LOG_PROGRAM, "ini value '%s' is section '%s' is not present\n", name.c_str(), section.c_str());
			throw std::exception();
		}
	} else {
		PDEBUGF(LOG_V2, LOG_PROGRAM, "ini section '%s' is not present\n", section.c_str());
		throw std::exception();
	}
	return value;
}

string AppConfig::get(const string &section, const string &name)
{
	string valstr;
	try {
		valstr = get(m_values, section, name);
	} catch(std::exception &e) {
		try {
			valstr = get(ms_def_values, section, name);
		} catch(std::exception &e) {
			PERRF(LOG_PROGRAM, "[%s]:%s is not a valid configuration key!\n", section.c_str(),name.c_str());
			throw;
		}
		PWARNF(LOG_PROGRAM, "[%s]:%s undefined, loading default: '%s'\n", section.c_str(),name.c_str(), valstr.c_str());
	}
	return valstr;
}

long AppConfig::get_int(const string &_section, const string &_name)
{
	string valstr;
	long value;

	try {
		valstr = get(_section, _name);
		value = parse_int(valstr);
	} catch(std::exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get integer value for [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	return value;
}

double AppConfig::get_real(const string &_section, const string &_name)
{
	string valstr;
	double value;

	try {
		valstr = get(_section, _name);
		value = parse_real(valstr);
	} catch(std::exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get real value for [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	return value;
}

bool AppConfig::get_bool(const string &_section, const string &_name)
{
	string valstr;
	bool value;

	try {
		valstr = get(_section, _name);
		value = parse_bool(valstr);
	} catch(std::exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get bool value for [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	return value;
}

void AppConfig::set_bool(const string &_section, const string &_name, bool _value)
{
	m_values[make_key(_section)][make_key(_name)] = _value?"yes":"no";
}

string AppConfig::get_string(const string &_section, const string &_name)
{
	string val;
	try {
		val = get(_section, _name);
	} catch(std::exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	return val;
}

void AppConfig::set_string(const string &_section, const string &_name, string _value)
{
	m_values[make_key(_section)][make_key(_name)] = _value;
}

string AppConfig::get_file_path(const string &_filename, FileType _type)
{
#ifndef _WIN32
	if(_filename.at(0) == '~') {
		return m_user_home + _filename.substr(1);
	} else if(_filename.at(0) == '/') {
		return _filename;
	}
#else
	std::regex re("^([A-Za-z]):(\\\\|\\/)", std::regex::ECMAScript|std::regex::icase);
	if(std::regex_search(_filename, re)) {
		return _filename;
	}
#endif

	switch(_type) {
		case FILE_TYPE_ASSET: {
			return m_assets_home + FS_SEP + _filename;
		}
		case FILE_TYPE_USER: {
			return m_cfg_home + FS_SEP + _filename;
		}
		case FILE_TYPE_MEDIA: {
			return get_file(PROGRAM_SECTION, PROGRAM_MEDIA_DIR, FILE_TYPE_USER)
					+ FS_SEP + _filename;
		}
	}
	return _filename;
}

string AppConfig::get_file(const string &_section, const string &_name, FileType _type)
{
	string filename;
	try {
		filename = get(_section, _name);
	} catch(std::exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get string [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	if(filename.empty()) {
		return filename;
	}

	return get_file_path(filename, _type);
}

string AppConfig::find_file(const string &_section, const string &_name)
{
	string path = get_file(_section, _name, FILE_TYPE_USER);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file(_section, _name, FILE_TYPE_ASSET);
	}
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file(_section, _name, FILE_TYPE_MEDIA);
	}
	return path;
}

string AppConfig::find_media(const string &_section, const string &_name)
{
	string path = get_file(_section, _name, FILE_TYPE_MEDIA);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file(_section, _name, FILE_TYPE_USER);
	}
	return path;
}

uint AppConfig::get_enum(const string &_section, const string &_name, ini_enum_map_t &_enum_map)
{
	string enumstr;
	try {
		enumstr = get(_section, _name);
	} catch(std::exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	auto enumvalue = _enum_map.find(enumstr);
	if(enumvalue == _enum_map.end()) {
		PERRF(LOG_PROGRAM, "unable to find enum value for '%s' in [%s]:%s\n",
				enumstr.c_str(), _section.c_str(), _name.c_str());
		throw std::exception();
	}
	return enumvalue->second;
}

string AppConfig::make_key(string name)
{
	// Convert to lower case to make section/name lookups case-insensitive
	// no, don't do this, keep it case sensitive
	//std::transform(name.begin(), name.end(), name.begin(), ::tolower);
	return name;
}

int AppConfig::value_handler(void* _user, const char* _section, const char* _name,
                            const char* _value)
{
	AppConfig* reader = static_cast<AppConfig*>(_user);

	string s = make_key(_section);
	string n = make_key(_name);


	ini_section_t & sec = reader->m_values[s];
	sec[n] = _value;

	PDEBUGF(LOG_V2, LOG_PROGRAM, "config [%s]:%s=%s\n",
			_section, _name, _value);

	return 1;
}

void AppConfig::create_file(const std::string &_filename, bool _comments)
{
	std::ofstream file(_filename.c_str());
	if(!file.is_open()) {
		PERRF(LOG_FS,"unable to open '%s' for writing\n",_filename.c_str());
		throw std::exception();
	}
	if(_comments) {
		file << ms_help["HEADER"] << std::endl;
	}
	for(auto section : ms_keys_order) {
		file << "[" << section.first << "]" << std::endl;
		if(_comments) {
			file << ms_help[section.first];
		}

		for(auto key : section.second) {
			file << key << "=" << get(section.first, key) << std::endl;
		}
		file << std::endl;
	}

	file.close();
}

