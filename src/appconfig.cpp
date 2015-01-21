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

#include "ibmulator.h"
#include "appconfig.h"
#include "program.h"
#include <algorithm>
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
		{ GUI_KEYMAP, "keymaps/pc-it.map"},
		{ GUI_MOUSE_TYPE, "ps2" },
		{ GUI_MOUSE_GRAB, "yes" },
		{ GUI_GRAB_METHOD, "MOUSE3" }, //CTRL-F10 or MOUSE3
		{ GUI_FB_VERTEX_SHADER, "gui/shaders/fb-passthrough.vs" },
		{ GUI_FB_FRAGMENT_SHADER, "gui/shaders/fb-nearest.fs" },
		{ GUI_GUI_VERTEX_SHADER, "gui/shaders/gui.vs" },
		{ GUI_GUI_FRAGMENT_SHADER, "gui/shaders/gui.fs" },
		{ GUI_SCREEN_DPI, "96" },
		{ GUI_START_IMAGE, "" },
		{ GUI_WIDTH, "640" },
		{ GUI_HEIGHT, "480" },
		{ GUI_FULLSCREEN, "no" },
		{ GUI_SHOW_LEDS, "yes" },
		{ GUI_MODE, "normal" },
		{ GUI_SAMPLER, "linear" },
		{ GUI_ASPECT, "original" },
		{ GUI_BG_R, "59" },
		{ GUI_BG_G, "82" },
		{ GUI_BG_B, "98" }
	} },

	{ CMOS_SECTION, {
		{ CMOS_IMAGE_FILE, "cmos.bin" },
		{ CMOS_IMAGE_RTC_INIT, "no" },
		{ CMOS_IMAGE_SAVE, "yes" }
	} },

	{ MEM_SECTION, {
		{ MEM_BASE_SIZE, "640" },
		{ MEM_EXT_SIZE, "384" },
		{ MEM_F80000_IMAGE_FILE, "F80000.BIN" },
		{ MEM_FC0000_IMAGE_FILE, "FC0000.BIN" }
	} },

	{ DRIVES_SECTION, {
		{ DRIVES_FDD_A, "3.5" },
		{ DRIVES_FDD_B, "3.5" },
		{ DRIVES_HDD, "none" }
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
		{ DISK_READONLY, "yes" },
		{ DISK_PATH, "" }
	} },

	{ DISK_C_SECTION, {
		{ DISK_TYPE, "30M" },
		{ DISK_INSERTED, "no" },
		{ DISK_READONLY, "yes" },
		{ DISK_PATH, "hdd.img" }
	} },

	{ MIXER_SECTION, {
		{ MIXER_RATE, "44100" },
		{ MIXER_SAMPLES, "1024" },
		{ MIXER_PREBUFFER, "25" }
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
"# This is the configuration file for " PACKAGE_STRING "\n"
"# Lines starting with a # are comment lines and are ignored. "
" They are used to document the effect of each option.\n"
"# Anywhere a path is involved, it can be absolute or relative.\n"
"# Relative paths are relative to the user directory (the folder where this file should be located)"
" or the program assets directory. Files and directories are searched in this order.\n"
		},
		{ PROGRAM_SECTION,
"#   media_dir: The default directory path used to search for floppy/hdd images.\n"
"# capture_dir: Directory where things like wave, savestate and screenshot get captured.\n"
		},

		{ CPU_SECTION,
"# frequency: The CPU frequency in MHz.\n"
		},

		{ GUI_SECTION,
"#      aspect: VGA aspect ratio.\n"
"#              Possible values: original, adaptive, scaled.\n"
"#      bg_XXX: Background window color\n"
"#         dpi: Resolution of the host display in DPI (currently used for mouse acceleration).\n"
"#  fbfragment: GLSL fragment shader to use for VGA rendering.\n"
"#    fbvertex: GLSL vertex shader to use for VGA rendering.\n"
"#  fullscreen: Start directly in fullscreen. (Press ALT-Enter to go back)\n"
"#        grab: If no then the mouse will not be hidden when grabbed (useful when debugging IBMulator)\n"
"# grab_method: Method to use for mouse grabbing\n"
"#              Possible values: MOUSE3, CTRL-F10\n"
"# guifragment: GLSL fragment shader for GUI rendering\n"
"#   guivertex: GLSL vertex shader for GUI rendering\n"
"#       width: VGA window width\n"
"#      height: VGA window height\n"
"#      keymap: Keymap table file. The file format is taken from Bochs, with some differences.\n"
"#              Open a .map file to read comments on how to edit it.\n"
"#        mode: Possible values: normal, compact.\n"
"#               normal: the system unit places itself at the bottom of the display and is always visible\n"
"#              compact: the system unit disappears when input is grabbed or CTRL-F1 is pressed\n"
"#       mouse: Mouse type.\n"
"#              Possible values: none, ps2, serial\n"
"#     sampler: VGA scaling quality.\n"
"#              Possible values: linear, nearest\n"
"#   show_leds: Show or hides the drives motor activity led at the bottom-right (useful in compact mode)\n"
"# start_image: An optional PNG file to load at program start\n"

		},

		{ CMOS_SECTION, ""
"#      image: Path of the binary file to use for the CMOS initialisation values.\n"
"# image_init: Yes if you want to initialise the RTC with the values in the CMOS image\n"
"# image_save: Yes if you want to save the CMOS in the image file when the machine is powered off\n"
		},

		{ MEM_SECTION,
"#   FC0000: Path of the BIOS ROM\n"
"#   F80000: Path of the regional ROM (optional, use only for non US versions)\n"
"#     base: Size of the base RAM in kebibytes\n"
"# extended: Size of the extended RAM in kebibytes\n"
		},

		{ DRIVES_SECTION,
"# floppy_a: The type of floppy drive A.\n"
"#           Possible values: none, 3.5, 5.25\n"
"# floppy_b: The type of floppy drive B.\n"
"#           Possible values: none, 3.5, 5.25\n"
"#      hdd: The type of the HDD\n"
"#           Possible values: none\n"
		},

		{ DISK_A_SECTION,
"# These options are used to mount a floppy at program launch.\n"
"#     path: Path of a floppy image file to mount at program lauch\n"
"# inserted: Yes if the floppy is inserted at program lauch\n"
"# readonly: Yes if the floppy image should be write protected\n"
"#     type: The type of the inserted floppy.\n"
"#           Possible values: none, 1.44M, 720K, 1.2M, 360K\n"
		},

		{ DISK_B_SECTION,
"# These options are used to mount a floppy at program launch.\n"
"#     path: Path of a floppy image file to mount at program lauch\n"
"# inserted: Yes if the floppy is inserted at program lauch\n"
"# readonly: Yes if the floppy image should be write protected\n"
"#     type: The type of the inserted floppy.\n"
"#           Possible values: none, 1.44M, 720K, 1.2M, 360K\n"
		},

		{ DISK_C_SECTION, "# HDD emulation is not yet implemented. Do not use.\n"},

		{ MIXER_SECTION,
"# prebuffer: How many milliseconds of data to prebuffer before audio start to be emitted.\n"
"#   samples: Audio samples buffer size; a larger buffer might help sound stuttering.\n"
"#            Possible values: 1024, 2048, 4096, 8192, 512, 256.\n"
"#      rate: Sample rate.\n"
"#            Possible values: 44100, 48000, 32000, 22050.\n"
		},

		{ COM_SECTION, "" },

		{ LPT_SECTION, "" },
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
	m_values.insert(_config.m_values.begin(), _config.m_values.end());
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

string AppConfig::get_file_path(const string &_filename, bool _asset)
{
	//TODO: Windows?
	string fpath = _filename;
	if(_filename.at(0) == '~') {
		fpath = m_user_home + _filename.substr(1);
	} else if(_filename.at(0) != '/') {
		if(_asset) {
			fpath = m_assets_home + FS_SEP + _filename;
		} else {
			fpath = m_cfg_home + FS_SEP + _filename;
		}
	}
	return fpath;
}

string AppConfig::get_file(const string &_section, const string &_name, bool _asset)
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

	return get_file_path(filename, _asset);
}

string AppConfig::find_file(const string &_section, const string &_name)
{
	string path = get_file(_section, _name, false);
	if(!Program::file_exists(path.c_str())) {
		path = get_file(_section, _name, true);
	}
	return path;
}

uint AppConfig::get_enum(const string &_section, const string &_name, ini_enum_map_t &_enum_map)
{
	string enumstr;
	try {
		enumstr = get(_section, _name);
	} catch(exception &e) {
		PERRF_ABORT(LOG_PROGRAM, "unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
	}

	auto enumvalue = _enum_map.find(enumstr);
	if(enumvalue == _enum_map.end()) {
		PERRF(LOG_PROGRAM, "unable to find enum value for '%s' in [%s]:%s\n",
				enumstr.c_str(), _section.c_str(), _name.c_str());
		throw exception();
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
		file << ms_help["HEADER"] << endl;
	}
	for(auto section : ms_def_values) {
		file << "[" << section.first << "]" << endl;
		if(_comments) {
			file << ms_help[section.first];
		}
		for(auto value : section.second) {
			file << value.first << "=" << get(section.first, value.first) << endl;
		}
		file << endl;
	}

	file.close();
}

