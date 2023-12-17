/*
 * Copyright (C) 2015-2023  Marco Bortolin
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
#include "ini.h"


enum FileType {
	FILE_TYPE_ASSET,
	FILE_TYPE_USER,
	FILE_TYPE_MEDIA
};

enum ConfigType {
	PROGRAM_CONFIG,
	MACHINE_CONFIG,
	MIXER_CONFIG,
	ANY_CONFIG
};

enum ConfigVisibility {
	PUBLIC_CFGKEY,
	HIDDEN_CFGKEY,
};

class AppConfig : public INIFile
{
public:
	using ConfigPair = std::pair<std::string, std::string>;

private:
	std::string m_user_home;
	std::string m_cfg_home;
	std::string m_assets_home;
	std::string m_assets_shaders_path;
	std::string m_user_shaders_path;
	std::string m_images_path;
	
	struct ConfigKeyInfo {
		std::string name;
		ConfigType type;
		ConfigVisibility visibility;
		std::string default_value;
	};
	struct ConfigSectionInfo {
		std::string name;
		std::vector<ConfigKeyInfo> keys;
	};
	using ConfigSections = std::vector<ConfigSectionInfo>;
	using ConfigKeys = std::map<std::string, std::map<std::string, ConfigKeyInfo>>;
	using ConfigHelp = std::map<std::string, std::string>;
	
	static ConfigSections ms_sections;
	static ConfigKeys ms_keys;
	static ConfigHelp ms_help;
	static ini_file_t ms_default_values;

public:
	AppConfig();
	
	void set_user_home(std::string _path);
	void set_cfg_home(std::string _path);
	void set_assets_home(std::string _path);
	std::string get_assets_home() const { return m_assets_home; }
	std::string get_user_home() const { return m_user_home; }
	std::string get_cfg_home() const { return m_cfg_home; }

	std::string get_value(const std::string &_section, const std::string &_name);
	std::string get_value(const std::string &_section, const std::string &_name, bool _quiet);
	std::string get_value_default(const std::string &_section, const std::string &_name) noexcept;

	int get_int_default(const std::string &section, const std::string &name) noexcept;
	double get_real_default(const std::string &section, const std::string &name) noexcept;
	bool get_bool_default(const std::string &section, const std::string &name) noexcept;

	int get_int_or_default(const std::string &section, const std::string &name) noexcept;
	double get_real_or_default(const std::string &section, const std::string &name) noexcept;
	int get_int_or_default(const std::string &section, const std::string &name, int min, int max) noexcept;
	double get_real_or_default(const std::string &section, const std::string &name, double min, double max) noexcept;
	bool get_bool_or_default(const std::string &section, const std::string &name) noexcept;
	std::string get_string_or_default(const std::string &section, const std::string &name) noexcept;

	std::string get_file(const std::string &_section, const std::string &_name, FileType _type);
	std::string try_get_file(const std::string &_section, const std::string &_name, FileType _type);
	std::string get_file_path(std::string _filename, FileType _type);
	std::string find_file(const std::string &_section, const std::string &_name);
	std::string find_file(const std::string &_filename);
	std::string find_media(const std::string &_section, const std::string &_name);
	std::string find_media(const std::string &_filename);

	void create_file(const std::string &_filename, ConfigType _type, bool _comments, bool _savestate);

	void reset();
	void merge(const AppConfig &_config, ConfigType _type);
	void copy(const AppConfig &_config);

	std::string get_assets_shaders_path() const { return m_assets_shaders_path; }
	std::string get_users_shaders_path() const { return m_user_shaders_path; }
	std::string get_images_path() const { return m_images_path; }
	
	std::string find_shader_asset(std::string _relative_file_path);
	std::string find_shader_asset_relative_to(std::string _relative_file_path, std::string _relative_to_abs_file);
};


#define PROGRAM_SECTION         "program"
#define PROGRAM_MEDIA_DIR       "media_dir"
#define PROGRAM_WAIT_METHOD     "timing"
#define PROGRAM_LOG_FILE        "log_file"

#define GUI_SECTION             "gui"
#define GUI_RENDERER            "renderer"
#define GUI_FRAMECAP            "framecap"
#define GUI_KEYMAP              "keymap"
#define GUI_MOUSE_TYPE          "mouse"
#define GUI_MOUSE_GRAB          "grab"
#define GUI_WIDTH               "width"
#define GUI_HEIGHT              "height"
#define GUI_FULLSCREEN          "fullscreen"
#define GUI_SHOW_INDICATORS     "show_indicators"
#define GUI_MODE                "mode"
#define GUI_COMPACT_TIMEOUT     "compact_timeout"
#define GUI_REALISTIC_ZOOM      "realistic_zoom"
#define GUI_REALISTIC_STYLE     "realistic_style"
#define GUI_BG_R                "bg_red"
#define GUI_BG_G                "bg_green"
#define GUI_BG_B                "bg_blu"
#define GUI_UI_SCALING          "ui_scaling"

#define DIALOGS_SECTION         "gui_dialogs"
#define DIALOGS_FILE_TYPE       "file_type"
#define DIALOGS_FILE_MODE       "file_mode"
#define DIALOGS_FILE_ORDER      "file_order"
#define DIALOGS_FILE_ZOOM       "file_zoom"
#define DIALOGS_SAVE_MODE       "save_mode"
#define DIALOGS_SAVE_ORDER      "save_order"
#define DIALOGS_SAVE_ZOOM       "save_zoom"
#define DIALOGS_OSD_TIMEOUT     "osd_timeout"
#define DIALOGS_VU_METERS       "vu_meters"

#define CAPTURE_SECTION         "capture"
#define CAPTURE_DIR             "directory"
#define CAPTURE_VIDEO_MODE      "video_mode"
#define CAPTURE_VIDEO_FORMAT    "video_format"
#define CAPTURE_VIDEO_QUALITY   "video_quality"

#define DISPLAY_SECTION          "display"
#define DISPLAY_TYPE             "type"
#define DISPLAY_NORMAL_SCALE     "normal_scale"
#define DISPLAY_NORMAL_ASPECT    "normal_aspect"
#define DISPLAY_NORMAL_SHADER    "normal_shader"
#define DISPLAY_REALISTIC_SHADER "realistic_shader"
#define DISPLAY_FILTER           "upscaling_filter"
#define DISPLAY_BRIGHTNESS       "brightness"
#define DISPLAY_CONTRAST         "contrast"
#define DISPLAY_SATURATION       "saturation"
#define DISPLAY_AMBIENT          "ambient_light"
#define DISPLAY_SAMPLERS_MODE    "samplers_mode"
#define DISPLAY_SHADER_INPUT     "shader_input_size"
#define DISPLAY_SHADER_OUTPUT    "shader_output_size"

#define SYSTEM_SECTION          "system"
#define SYSTEM_ROMSET           "romset"
#define SYSTEM_MODEL            "model"
#define SYSTEM_BIOS_PATCHES     "bios_patches"

#define CPU_SECTION             "cpu"
#define CPU_MODEL               "model"
#define CPU_FREQUENCY           "frequency"
#define CPU_HLT_WAIT            "hlt_wait"

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
#define DRIVES_FLOPPY_COMMIT    "floppy_commit"
#define DRIVES_FDD_LAT          "fdd_latency"
#define DRIVES_FDC_TYPE         "fdc_type"
#define DRIVES_FDC_MODE         "fdc_mode"
#define DRIVES_FDC_OVR          "fdc_overhead"
#define DRIVES_HDC_TYPE         "hdc_type"
#define DRIVES_HDD_COMMIT       "hdd_commit"

#define DISK_A_SECTION          "floppy_a"
#define DISK_B_SECTION          "floppy_b"
#define DISK_C_SECTION          "hdd"
#define DISK_CD_SECTION         "cdrom"
#define DISK_TYPE               "type"
#define DISK_INSERTED           "inserted"
#define DISK_READONLY           "readonly"
#define DISK_PATH               "path"
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
#define MIXER_PROFILE           "profile"
#define MIXER_VOLUME            "volume"

#define MIDI_SECTION            "midi"
#define MIDI_ENABLED            "enabled"
#define MIDI_DEVICE             "device"
#define MIDI_DEVTYPE            "device_type"
#define MIDI_DELAY              "sysex_delay"

#define PCSPEAKER_SECTION       "pcspeaker"
#define PCSPEAKER_LEVEL         "level"
#define PCSPEAKER_FILTERS       "filters"
#define PCSPEAKER_VOLUME        "volume"
#define PCSPEAKER_BALANCE       "balance"
#define PCSPEAKER_REVERB        "reverb"
#define PCSPEAKER_CHORUS        "chorus"

#define PS1AUDIO_SECTION        "ps1audio"
#define PS1AUDIO_ENABLED        "enabled"
#define PS1AUDIO_PSG_RATE       "psg_rate"
#define PS1AUDIO_PSG_FILTERS    "psg_filters"
#define PS1AUDIO_PSG_VOLUME     "psg_volume"
#define PS1AUDIO_PSG_BALANCE    "psg_balance"
#define PS1AUDIO_PSG_REVERB     "psg_reverb"
#define PS1AUDIO_PSG_CHORUS     "psg_chorus"
#define PS1AUDIO_DAC_FILTERS    "dac_filters"
#define PS1AUDIO_DAC_VOLUME     "dac_volume"
#define PS1AUDIO_DAC_BALANCE    "dac_balance"
#define PS1AUDIO_DAC_REVERB     "dac_reverb"
#define PS1AUDIO_DAC_CHORUS     "dac_chorus"

#define ADLIB_SECTION           "adlib"
#define ADLIB_ENABLED           "enabled"
#define ADLIB_RATE              "rate"
#define ADLIB_FILTERS           "filters"
#define ADLIB_VOLUME            "volume"
#define ADLIB_BALANCE           "balance"
#define ADLIB_REVERB            "reverb"
#define ADLIB_CHORUS            "chorus"

#define SBLASTER_SECTION        "sblaster"
#define SBLASTER_ENABLED        "enabled"
#define SBLASTER_MODEL          "model"
#define SBLASTER_IOBASE         "iobase"
#define SBLASTER_DMA            "dma"
#define SBLASTER_IRQ            "irq"
#define SBLASTER_DAC_RESAMPLING "dac_resampling"
#define SBLASTER_DAC_FILTERS    "dac_filters"
#define SBLASTER_DAC_REVERB     "dac_reverb"
#define SBLASTER_DAC_CHORUS     "dac_chorus"
#define SBLASTER_DAC_CROSSFEED  "dac_crossfeed"
#define SBLASTER_DAC_VOLUME     "dac_volume"
#define SBLASTER_DAC_BALANCE    "dac_balance"
#define SBLASTER_OPL_RATE       "opl_rate"
#define SBLASTER_OPL_FILTERS    "opl_filters"
#define SBLASTER_OPL_REVERB     "opl_reverb"
#define SBLASTER_OPL_CHORUS     "opl_chorus"
#define SBLASTER_OPL_CROSSFEED  "opl_crossfeed"
#define SBLASTER_OPL_VOLUME     "opl_volume"
#define SBLASTER_OPL_BALANCE    "opl_balance"

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
#define SOUNDFX_REVERB          "reverb"
#define SOUNDFX_FDD_SPIN        "fdd_spin"
#define SOUNDFX_FDD_SEEK        "fdd_seek"
#define SOUNDFX_FDD_GUI         "fdd_gui"
#define SOUNDFX_FDD_BALANCE     "fdd_balance"
#define SOUNDFX_HDD_SPIN        "hdd_spin"
#define SOUNDFX_HDD_SEEK        "hdd_seek"
#define SOUNDFX_HDD_BALANCE     "hdd_balance"
#define SOUNDFX_SYSTEM          "system"
#define SOUNDFX_SYSTEM_BALANCE  "system_balance"
#define SOUNDFX_MODEM           "modem"
#define SOUNDFX_MODEM_COUNTRY   "modem_country"
#define SOUNDFX_MODEM_FILTERS   "modem_filters"
#define SOUNDFX_MODEM_BALANCE   "modem_balance"

#define SERIAL_SECTION          "serial"
#define SERIAL_ENABLED          "enabled"
#define SERIAL_A_MODE           "mode"
#define SERIAL_A_DEV            "dev"
#define SERIAL_A_TX_DELAY       "tx_delay"
#define SERIAL_A_TCP_NODELAY    "tcp_nodelay"
#define SERIAL_A_DUMP           "dump_file"
#define SERIAL_B_MODE           "b_mode"
#define SERIAL_B_DEV            "b_dev"
#define SERIAL_B_TX_DELAY       "b_tx_delay"
#define SERIAL_B_TCP_NODELAY    "b_tcp_nodelay"
#define SERIAL_B_DUMP           "b_dump_file"
#define SERIAL_C_MODE           "c_mode"
#define SERIAL_C_DEV            "c_dev"
#define SERIAL_C_TX_DELAY       "c_tx_delay"
#define SERIAL_C_TCP_NODELAY    "c_tcp_nodelay"
#define SERIAL_C_DUMP           "c_dump_file"
#define SERIAL_D_MODE           "d_mode"
#define SERIAL_D_DEV            "d_dev"
#define SERIAL_D_TX_DELAY       "d_tx_delay"
#define SERIAL_D_TCP_NODELAY    "d_tcp_nodelay"
#define SERIAL_D_DUMP           "d_dump_file"

#define MODEM_SECTION           "modem"
#define MODEM_BAUD_RATE         "baud_rate"
#define MODEM_LISTEN_ADDR       "listen_addr"
#define MODEM_PHONEBOOK         "phonebook_file"
#define MODEM_TELNET_MODE       "telnet_mode"
#define MODEM_CONN_TIMEOUT      "conn_timeout"
#define MODEM_WARM_DELAY        "warmup_delay"
#define MODEM_CONNECT_CODE      "connect_code"
#define MODEM_ECHO_ON           "echo_on"
#define MODEM_HANDSHAKE         "handshake"
#define MODEM_DUMP              "dump_file"

#define LPT_SECTION             "lpt"
#define LPT_ENABLED             "enabled"
#define LPT_PORT                "port"
#define LPT_FILE                "file"

#define PRN_SECTION             "printer"
#define PRN_CONNECTED           "connected"
#define PRN_MODE                "interpreter"
#define PRN_COLOR               "color"
#define PRN_INK                 "ink"
#define PRN_PREVIEW_DIV         "preview"
#define PRN_EPSON_CSET          "epson_charset"
#define PRN_IBM_CSET            "ibm_charset"
#define PRN_PAPER_SIZE          "paper_size"
#define PRN_PAPER_TYPE          "paper_type"
#define PRN_BOF                 "bottom_of_form"
#define PRN_TOP_MARGIN          "top_magin"
#define PRN_BOTTOM_MARGIN       "bottom_magin"
#define PRN_SHOW_HEAD           "show_head_pos"

#define LOG_SECTION             "log"
#define LOG_OVERRIDE_VERBOSITY  "override"
#define LOG_PROGRAM_VERBOSITY   "program"
#define LOG_FS_VERBOSITY        "fs"
#define LOG_GFX_VERBOSITY       "gfx"
#define LOG_INPUT_VERBOSITY     "input"
#define LOG_GUI_VERBOSITY       "gui"
#define LOG_OGL_VERBOSITY       "ogl"
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
#define LOG_PRN_VERBOSITY       "prn"
#define LOG_COM_VERBOSITY       "com"
#define LOG_MIDI_VERBOSITY      "midi"
#define LOG_NET_VERBOSITY       "net"

#endif

