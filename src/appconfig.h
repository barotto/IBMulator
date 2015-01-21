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


#ifndef IBMULATOR_APPCONFIG_H
#define IBMULATOR_APPCONFIG_H

#include <map>
#include <string>

typedef std::map<std::string, uint> ini_enum_map_t;
typedef std::map<std::string, std::string> ini_section_t;
typedef std::map<std::string, ini_section_t> ini_file_t;
typedef std::map<std::string, std::string> ini_filehelp_t;

class AppConfig
{
private:
	std::string m_user_home;
	std::string m_cfg_home;
	std::string m_assets_home;
	int m_error;

	ini_file_t m_values;
	static ini_file_t ms_def_values;
	static ini_filehelp_t ms_help;

	static std::string make_key(std::string _name);
	static int value_handler(void* user, const char* section, const char* name, const char* value);

	std::string get(ini_file_t &_values, const string &_section, const string &_name);
	std::string get(const std::string &_section, const std::string &_name);

	long parse_int(const std::string &_str);
	double parse_real(const std::string &_str);
	bool parse_bool(std::string _str);

public:
	AppConfig();

	void set_user_home(std::string _path) { m_user_home = _path; }
	void set_cfg_home(std::string _path) { m_cfg_home = _path; }
	void set_assets_home(std::string _path) { m_assets_home = _path; }
	std::string get_assets_home() { return m_assets_home; }
	std::string get_user_home() { return m_user_home; }
	std::string get_cfg_home() { return m_cfg_home; }

	void parse(const std::string &_filename);

	// Return the result of ini_parse(), i.e., 0 on success, line number of
	// first error on parse error, or -1 on file open error.
	int get_error();

	long get_int(const std::string &section, const std::string &name);
	double get_real(const std::string &section, const std::string &name);
	bool get_bool(const std::string &section, const std::string &name);
	string get_string(const string &_section, const string &_name);
	uint get_enum(const string &_section, const string &_name, ini_enum_map_t &_enum_map);
	std::string get_file(const std::string &_section, const std::string &_name, bool _asset);
	string get_file_path(const string &_filename, bool _asset);
	string find_file(const string &_section, const string &_name);

	void set_bool(const std::string &section, const std::string &name, bool _value);
	void set_string(const std::string &_section, const std::string &_name, std::string _value);

	void create_file(const std::string &_filename, bool _comments=false);

	void reset();
	void merge(AppConfig &_config);
};


#define PROGRAM_SECTION     "program"
#define PROGRAM_MEDIA_DIR   "media_dir"
#define PROGRAM_CAPTURE_DIR "capture_dir"

#define CPU_SECTION         "cpu"
#define CPU_FREQUENCY       "frequency"

#define GUI_SECTION             "gui"
#define GUI_KEYMAP              "keymap"
#define GUI_MOUSE_TYPE          "mouse"
#define GUI_MOUSE_GRAB          "grab"
#define GUI_GRAB_METHOD         "grab_method"
#define GUI_FB_VERTEX_SHADER    "fbvertex"
#define GUI_FB_FRAGMENT_SHADER  "fbfragment"
#define GUI_GUI_VERTEX_SHADER   "guivertex"
#define GUI_GUI_FRAGMENT_SHADER "guifragment"
#define GUI_START_IMAGE         "start_image"
#define GUI_SCREEN_DPI          "dpi"
#define GUI_WIDTH               "width"
#define GUI_HEIGHT              "height"
#define GUI_FULLSCREEN          "fullscreen"
#define GUI_SHOW_LEDS           "show_leds"
#define GUI_MODE                "mode"
#define GUI_SAMPLER             "sampler"
#define GUI_ASPECT              "aspect"
#define GUI_BG_R                "bg_red"
#define GUI_BG_G                "bg_green"
#define GUI_BG_B                "bg_blu"

#define CMOS_SECTION        "cmos"
#define CMOS_IMAGE_FILE     "image"
#define CMOS_IMAGE_RTC_INIT "image_init"
#define CMOS_IMAGE_SAVE     "image_save"

#define MEM_SECTION           "memory"
#define MEM_BASE_SIZE         "base"
#define MEM_EXT_SIZE          "extended"
#define MEM_F80000_IMAGE_FILE "F80000"
#define MEM_FC0000_IMAGE_FILE "FC0000"

#define DRIVES_SECTION  "drives"
#define DRIVES_FDD_A    "floppy_a"
#define DRIVES_FDD_B    "floppy_b"
#define DRIVES_HDD      "hdd"

#define DISK_A_SECTION  "floppy_a"
#define DISK_B_SECTION  "floppy_b"
#define DISK_C_SECTION  "hdd"
#define DISK_TYPE       "type"
#define DISK_INSERTED   "inserted"
#define DISK_READONLY   "readonly"
#define DISK_PATH       "path"

#define MIXER_SECTION   "mixer"
#define MIXER_RATE      "rate"
#define MIXER_PREBUFFER "prebuffer"
#define MIXER_SAMPLES   "samples"


#define COM_SECTION "com"
#define COM_ENABLED "enabled"
#define COM_MODE    "mode"
#define COM_DEV     "dev"

#define LPT_SECTION "lpt"
#define LPT_ENABLED "enabled"
#define LPT_PORT    "port"
#define LPT_FILE    "file"

#endif

