/*
 * Copyright (C) 2015-2021  Marco Bortolin
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


#ifndef IBMULATOR_APPCONFIG_H
#define IBMULATOR_APPCONFIG_H

#include <map>
#include <string>
#include <vector>
#include <set>

typedef std::map<std::string, uint> ini_enum_map_t;
typedef std::map<std::string, std::string> ini_section_t;
typedef std::map<std::string, ini_section_t> ini_file_t;
typedef std::map<std::string, std::string> ini_filehelp_t;

enum FileType {
	FILE_TYPE_ASSET,
	FILE_TYPE_USER,
	FILE_TYPE_MEDIA
};

enum ConfigType {
	PROGRAM_CONFIG,
	MACHINE_CONFIG,
	ANY_CONFIG
};

class AppConfig
{
private:
	std::string m_parsed_file;
	std::string m_user_home;
	std::string m_cfg_home;
	std::string m_assets_home;
	int m_error;

	ini_file_t m_values;
	static ini_file_t ms_def_values[2];
	static ini_filehelp_t ms_help;
	static std::vector<std::pair<std::string, std::vector<std::string>>> ms_keys_order;

	static std::string make_key(std::string _name);
	static int value_handler(void* user, const char* section, const char* name, const char* value);

	std::string get_value(ini_file_t &_values, const std::string &_section, const std::string &_name);

public:
	AppConfig();

	void set_user_home(std::string _path) { m_user_home = _path; }
	void set_cfg_home(std::string _path) { m_cfg_home = _path; }
	void set_assets_home(std::string _path) { m_assets_home = _path; }
	std::string get_parsed_file() const { return m_parsed_file; }
	std::string get_assets_home() const { return m_assets_home; }
	std::string get_user_home() const { return m_user_home; }
	std::string get_cfg_home() const { return m_cfg_home; }

	void parse(const std::string &_filename);
	
	static int parse_int(const std::string &_str);
	static double parse_real(const std::string &_str);
	static bool parse_bool(std::string _str);
	static std::vector<std::string> parse_tokens(std::string _str, std::string _regex_sep);
	
	std::string get_value(const std::string &_section, const std::string &_name);

	// Return the result of ini_parse(), i.e., 0 on success, line number of
	// first error on parse error, or -1 on file open error.
	int get_error();

	int try_int(const std::string &section, const std::string &name);
	int get_int(const std::string &section, const std::string &name);
	int get_int(const std::string &section, const std::string &name, int _default);
	double try_real(const std::string &section, const std::string &name);
	double get_real(const std::string &section, const std::string &name);
	double get_real(const std::string &section, const std::string &name, double _default);
	bool try_bool(const std::string &section, const std::string &name);
	bool get_bool(const std::string &section, const std::string &name);
	bool get_bool(const std::string &section, const std::string &name, bool _default);
	std::string get_string(const std::string &_section, const std::string &_name);
	std::string get_string(const std::string &_section, const std::string &_name, const std::string &_default);
	std::string get_string(const std::string &_section, const std::string &_name, const std::set<std::string> _allowed, const std::string &_default);
	unsigned get_enum(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map);
	unsigned get_enum(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map, unsigned _default);
	unsigned get_enum_quiet(const std::string &_section, const std::string &_name, const ini_enum_map_t &_enum_map);
	std::string get_file(const std::string &_section, const std::string &_name, FileType _type);
	std::string get_file_path(const std::string &_filename, FileType _type);
	std::string find_file(const std::string &_section, const std::string &_name);
	std::string find_media(const std::string &_section, const std::string &_name);
	std::string find_media(const std::string &_filename);

	void set_int(const std::string &_section, const std::string &_name, int _value);
	void set_real(const std::string &_section, const std::string &_name, double _value);
	void set_bool(const std::string &section, const std::string &name, bool _value);
	void set_string(const std::string &_section, const std::string &_name, std::string _value);

	void create_file(const std::string &_filename, bool _comments=false);

	void reset();
	void merge(const AppConfig &_config, ConfigType _type);
	void copy(const AppConfig &_config);
};


#define PROGRAM_SECTION         "program"
#define PROGRAM_MEDIA_DIR       "media_dir"
#define PROGRAM_FILE_DIALOGS    "file_dialogs"
#define PROGRAM_WAIT_METHOD     "timing"
#define PROGRAM_LOG_FILE        "log_file"

#define GUI_SECTION             "gui"
#define GUI_RENDERER            "renderer"
#define GUI_FRAMECAP            "framecap"
#define GUI_KEYMAP              "keymap"
#define GUI_MOUSE_TYPE          "mouse"
#define GUI_MOUSE_GRAB          "grab"
#define GUI_MOUSE_ACCELERATION  "mouse_accel"
#define GUI_SCREEN_DPI          "dpi"
#define GUI_WIDTH               "width"
#define GUI_HEIGHT              "height"
#define GUI_FULLSCREEN          "fullscreen"
#define GUI_SHOW_LEDS           "show_leds"
#define GUI_MODE                "mode"
#define GUI_REALISTIC_ZOOM      "realistic_zoom"
#define GUI_REALISTIC_STYLE     "realistic_style"
#define GUI_BG_R                "bg_red"
#define GUI_BG_G                "bg_green"
#define GUI_BG_B                "bg_blu"

#define CAPTURE_SECTION         "capture"
#define CAPTURE_DIR             "directory"
#define CAPTURE_VIDEO_MODE      "video_mode"
#define CAPTURE_VIDEO_FORMAT    "video_format"
#define CAPTURE_VIDEO_QUALITY   "video_quality"

#define DISPLAY_SECTION          "display"
#define DISPLAY_NORMAL_ASPECT    "normal_aspect"
#define DISPLAY_NORMAL_SHADER    "normal_shader"
#define DISPLAY_NORMAL_FILTER    "normal_filter"
#define DISPLAY_REALISTIC_SHADER "realistic_shader"
#define DISPLAY_REALISTIC_FILTER "realistic_filter"
#define DISPLAY_REALISTIC_AMBIENT "realistic_ambient"
#define DISPLAY_BRIGHTNESS       "brightness"
#define DISPLAY_CONTRAST         "contrast"
#define DISPLAY_SATURATION       "saturation"

#define SYSTEM_SECTION          "system"
#define SYSTEM_ROMSET           "romset"
#define SYSTEM_MODEL            "model"

#define CPU_SECTION             "cpu"
#define CPU_MODEL               "model"
#define CPU_FREQUENCY           "frequency"

#define MEM_SECTION             "memory"
#define MEM_RAM_EXP             "expansion"
#define MEM_RAM_SPEED           "speed"

#define CMOS_SECTION            "cmos"
#define CMOS_IMAGE_FILE         "image"
#define CMOS_IMAGE_RTC_INIT     "image_init"
#define CMOS_IMAGE_SAVE         "image_save"

#define VGA_SECTION             "vga"
#define VGA_ROM                 "rom"
#define VGA_PS_BIT_BUG          "ps_bit_bug"

#define DRIVES_SECTION          "drives"
#define DRIVES_FDD_A            "floppy_a"
#define DRIVES_FDD_B            "floppy_b"
#define DRIVES_FDD_LAT          "fdd_latency"
#define DRIVES_HDD              "hdd"

#define DISK_A_SECTION          "floppy_a"
#define DISK_B_SECTION          "floppy_b"
#define DISK_C_SECTION          "hdd"
#define DISK_CD_SECTION         "cdrom"
#define DISK_TYPE               "type"
#define DISK_INSERTED           "inserted"
#define DISK_READONLY           "readonly"
#define DISK_PATH               "path"
#define DISK_SAVE		        "save"
#define DISK_CYLINDERS          "cylinders"
#define DISK_HEADS              "heads"
#define DISK_SPT                "sectors"
#define DISK_SEEK_MAX           "seek_max"
#define DISK_SEEK_TRK           "seek_trk"
#define DISK_ROT_SPEED          "rot_speed"
#define DISK_INTERLEAVE         "interleave"
#define DISK_SPINUP_TIME        "spin_up_time"

#define MIXER_SECTION           "mixer"
#define MIXER_RATE              "rate"
#define MIXER_SAMPLES           "samples"
#define MIXER_PREBUFFER         "prebuffer"
#define MIXER_VOLUME            "volume"

#define MIDI_SECTION            "midi"
#define MIDI_ENABLED            "enabled"
#define MIDI_DEVICE             "device"
#define MIDI_DEVTYPE            "device_type"
#define MIDI_DELAY              "sysex_delay"

#define PCSPEAKER_SECTION       "pcspeaker"
#define PCSPEAKER_ENABLED       "enabled"
#define PCSPEAKER_RATE          "rate"
#define PCSPEAKER_FILTERS       "filters"
#define PCSPEAKER_VOLUME        "volume"

#define PS1AUDIO_SECTION        "ps1audio"
#define PS1AUDIO_ENABLED        "enabled"
#define PS1AUDIO_RATE           "rate"
#define PS1AUDIO_FILTERS        "filters"
#define PS1AUDIO_VOLUME         "volume"

#define ADLIB_SECTION           "adlib"
#define ADLIB_ENABLED           "enabled"
#define ADLIB_RATE              "rate"
#define ADLIB_FILTERS           "filters"
#define ADLIB_VOLUME            "volume"

#define SBLASTER_SECTION        "sblaster"
#define SBLASTER_ENABLED        "enabled"
#define SBLASTER_MODEL          "model"
#define SBLASTER_IOBASE         "iobase"
#define SBLASTER_DMA            "dma"
#define SBLASTER_IRQ            "irq"
#define SBLASTER_DAC_FILTERS    "dac_filters"
#define SBLASTER_DAC_VOLUME     "dac_volume"
#define SBLASTER_OPL_RATE       "opl_rate"
#define SBLASTER_OPL_FILTERS    "opl_filters"
#define SBLASTER_OPL_VOLUME     "opl_volume"

#define MPU401_SECTION          "mpu401"
#define MPU401_ENABLED          "enabled"
#define MPU401_IOBASE           "iobase"
#define MPU401_IRQ              "irq"
#define MPU401_MODE             "mode"

#define GAMEPORT_SECTION        "gameport"
#define GAMEPORT_ENABLED        "enabled"

#define SOUNDFX_SECTION         "soundfx"
#define SOUNDFX_ENABLED         "enabled"
#define SOUNDFX_VOLUME          "volume"
#define SOUNDFX_FDD_SPIN        "fdd_spin"
#define SOUNDFX_FDD_SEEK        "fdd_seek"
#define SOUNDFX_HDD_SPIN        "hdd_spin"
#define SOUNDFX_HDD_SEEK        "hdd_seek"
#define SOUNDFX_SYSTEM          "system"

#define SERIAL_SECTION          "serial"
#define SERIAL_ENABLED          "enabled"
#define SERIAL_A_MODE           "mode"
#define SERIAL_A_DEV            "dev"
#define SERIAL_B_MODE           "b_mode"
#define SERIAL_B_DEV            "b_dev"
#define SERIAL_C_MODE           "c_mode"
#define SERIAL_C_DEV            "c_dev"
#define SERIAL_D_MODE           "d_mode"
#define SERIAL_D_DEV            "d_dev"

#define LPT_SECTION             "lpt"
#define LPT_ENABLED             "enabled"
#define LPT_PORT                "port"
#define LPT_FILE                "file"

#define LOG_SECTION             "log"
#define LOG_OVERRIDE_VERBOSITY  "override"
#define LOG_PROGRAM_VERBOSITY   "program"
#define LOG_FS_VERBOSITY        "fs"
#define LOG_GFX_VERBOSITY       "gfx"
#define LOG_INPUT_VERBOSITY     "input"
#define LOG_GUI_VERBOSITY       "gui"
#define LOG_MACHINE_VERBOSITY   "machine"
#define LOG_MIXER_VERBOSITY     "mixer"
#define LOG_MEM_VERBOSITY       "mem"
#define LOG_CPU_VERBOSITY       "cpu"
#define LOG_MMU_VERBOSITY       "mmu"
#define LOG_PIT_VERBOSITY       "pit"
#define LOG_PIC_VERBOSITY       "pic"
#define LOG_DMA_VERBOSITY       "dma"
#define LOG_KEYB_VERBOSITY      "keyb"
#define LOG_VGA_VERBOSITY       "vga"
#define LOG_CMOS_VERBOSITY      "cmos"
#define LOG_FDC_VERBOSITY       "fdc"
#define LOG_HDD_VERBOSITY       "hdd"
#define LOG_AUDIO_VERBOSITY     "audio"
#define LOG_GAMEPORT_VERBOSITY  "gameport"
#define LOG_LPT_VERBOSITY       "lpt"
#define LOG_COM_VERBOSITY       "com"
#define LOG_MIDI_VERBOSITY      "midi"

#endif

