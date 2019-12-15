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
#include "utils.h"
#include "filesys.h"
#include "appconfig.h"
#include "program.h"
#include <algorithm>
#include <regex>
#include <cctype>
#include <cstdlib>
#include "ini/ini.h"
#include "hardware/devices/hdd.h"

using std::string;

ini_file_t AppConfig::ms_def_values[2] = {

// PROGRAM_CONFIG
{
	{ PROGRAM_SECTION, {
		{ PROGRAM_MEDIA_DIR,    ""    },
		{ PROGRAM_CAPTURE_DIR,  ""    },
		{ PROGRAM_THREADS_SYNC, "no"  },
		{ PROGRAM_VSYNC,        "no"  },
		{ PROGRAM_FRAMECAP,     "yes" }
	} },

	{ GUI_SECTION, {
		{ GUI_RENDERER,           "opengl" },
		{ GUI_KEYMAP,             ""       },
		{ GUI_MOUSE_TYPE,         "ps2"    },
		{ GUI_MOUSE_GRAB,         "yes"    },
		{ GUI_MOUSE_ACCELERATION, "no"     },
		{ GUI_GRAB_METHOD,        "MOUSE3" }, //CTRL-F10 or MOUSE3
		{ GUI_SCREEN_DPI,         "96"     },
		{ GUI_WIDTH,              "640"    },
		{ GUI_HEIGHT,             "480"    },
		{ GUI_FULLSCREEN,         "no"     },
		{ GUI_SHOW_LEDS,          "no"     },
		{ GUI_MODE,               "normal" },
		{ GUI_BG_R,               "59"     },
		{ GUI_BG_G,               "82"     },
		{ GUI_BG_B,               "98"     }
	} },

	{ DISPLAY_SECTION, {
		{ DISPLAY_NORMAL_ASPECT,     "original" },
		{ DISPLAY_NORMAL_SHADER,     "gui/shaders/fb-normal.fs" },
		{ DISPLAY_NORMAL_FILTER,     "bilinear" },
		{ DISPLAY_REALISTIC_SHADER,  "gui/shaders/fb-realistic.fs" },
		{ DISPLAY_REALISTIC_FILTER,  "bicubic"  },
		{ DISPLAY_REALISTIC_SCALE,   "1.0" },
		{ DISPLAY_REALISTIC_AMBIENT, "0.6" },
		{ DISPLAY_BRIGHTNESS,        "1.0" },
		{ DISPLAY_CONTRAST,          "1.0" },
		{ DISPLAY_SATURATION,        "1.0" }
	} },

	{ CMOS_SECTION, {
		{ CMOS_IMAGE_RTC_INIT, "no"  },
		{ CMOS_IMAGE_SAVE,     "yes" }
	} },

	{ DISK_C_SECTION, {
		{ DISK_READONLY,   "no"  },
		{ DISK_SAVE,       "yes" }
	} },

	{ MIXER_SECTION, {
		{ MIXER_RATE,      "48000" },
		{ MIXER_SAMPLES,   "1024"  },
		{ MIXER_PREBUFFER, "50"    },
		{ MIXER_VOLUME,    "1.0"   }
	} },

	{ PCSPEAKER_SECTION, {
		{ PCSPEAKER_RATE,    "48000" },
		{ PCSPEAKER_FILTERS, "LowPass,order=5,cutoff=5000|HighPass,order=5,cutoff=500" },
		{ PCSPEAKER_VOLUME,  "0.5"   }
	} },

	{ PS1AUDIO_SECTION, {
		{ PS1AUDIO_RATE,    "48000" },
		{ PS1AUDIO_FILTERS, ""      },
		{ PS1AUDIO_VOLUME,  "1.0"   }
	} },

	{ ADLIB_SECTION, {
		{ ADLIB_RATE,    "48000" },
		{ ADLIB_FILTERS, ""      },
		{ ADLIB_VOLUME,  "1.4"   }
	} },

	{ SOUNDFX_SECTION, {
		{ SOUNDFX_VOLUME,   "1.0" },
		{ SOUNDFX_FDD_SPIN, "0.4" },
		{ SOUNDFX_FDD_SEEK, "0.4" },
		{ SOUNDFX_HDD_SPIN, "0.4" },
		{ SOUNDFX_HDD_SEEK, "0.4" },
		{ SOUNDFX_SYSTEM,   "1.0" }
	} },

	{ COM_SECTION, {
		{ COM_MODE,    "null" },
		{ COM_DEV,     ""     }
	} },

	{ LPT_SECTION, {
		{ LPT_PORT,    "LPT1" },
		{ LPT_FILE,    ""     }
	} },
},

//MACHINE_CONFIG
{
	{ SYSTEM_SECTION, {
		{ SYSTEM_ROMSET, ""     },
		{ SYSTEM_MODEL,  "auto" }
	} },

	{ CPU_SECTION, {
		{ CPU_MODEL,     "auto" },
		{ CPU_FREQUENCY, "auto" }
	} },

	{ CMOS_SECTION, {
		{ CMOS_IMAGE_FILE, "auto" }
	} },

	{ MEM_SECTION, {
		{ MEM_RAM_EXP,   "auto" },
		{ MEM_RAM_SPEED, "auto" }
	} },

	{ VGA_SECTION, {
		{ VGA_ROM,           ""   },
		{ VGA_PS_BIT_BUG,    "no" }
	} },
	
	{ DRIVES_SECTION, {
		{ DRIVES_FDD_A,   "auto" },
		{ DRIVES_FDD_B,   "auto" },
		{ DRIVES_FDD_LAT, "1.0"  },
		{ DRIVES_HDD,     "auto" }
	} },

	{ DISK_A_SECTION, {
		{ DISK_TYPE,     "auto"  },
		{ DISK_INSERTED, "no"    },
		{ DISK_READONLY, "no"    },
		{ DISK_PATH,     ""      }
	} },

	{ DISK_B_SECTION, {
		{ DISK_TYPE,     "auto"  },
		{ DISK_INSERTED, "no"    },
		{ DISK_READONLY, "no"    },
		{ DISK_PATH,     ""      }
	} },

	{ DISK_C_SECTION, {
		{ DISK_TYPE,       "auto" },
		{ DISK_PATH,       "auto" },
		{ DISK_CYLINDERS,  "auto" },
		{ DISK_HEADS,      "auto" },
		{ DISK_SPT,        "auto" },
		{ DISK_SEEK_MAX,   "auto" },
		{ DISK_SEEK_TRK,   "auto" },
		{ DISK_ROT_SPEED,  "auto" },
		{ DISK_INTERLEAVE, "auto" },
		{ DISK_SPINUP_TIME,"auto" }
	} },

	{ PCSPEAKER_SECTION, {
		{ PCSPEAKER_ENABLED, "yes" }
	} },

	{ PS1AUDIO_SECTION, {
		{ PS1AUDIO_ENABLED, "yes" }
	} },

	{ ADLIB_SECTION, {
		{ ADLIB_ENABLED, "no" }
	} },

	{ COM_SECTION, {
		{ COM_ENABLED, "yes" }
	} },

	{ LPT_SECTION, {
		{ LPT_ENABLED, "yes" }
	} }
}
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
";    media_dir: The default directory used to search for floppy and hdd images.\n"
";  capture_dir: Directory where things like wave files, savestates and screenshots get captured.\n"
"; threads_sync: Enable per-frame threads synchronization. See the README for more info.\n"
";        vsync: Enable video vertical synchronization. See the README for more info.\n"
";     framecap: Limit displayed frames to current VGA frequency. See the README for more info.\n"
		},

		{ SYSTEM_SECTION,
"; romset: Path to a bin/zip file or directory containing the ROM set to use (for the correct format see the README)\n"
";  model: The PS/1 Model. This is also the machine configuration that's used to select proper values for any \"auto\" value in this file.\n"
";         Possible values: auto, or a machine model string\n"
";          auto: the model is determined by the romset\n"
";          For the list of supported models and their hardware configuration see " PACKAGE_NAME "'s project site.\n"
		},

		{ CPU_SECTION,
";     model: The CPU model.\n"
";            Possible values: auto, 286, 386SX, 386DX.\n"
"; frequency: Frequency in MHz.\n"
";            Possible values: auto, or an integer number.\n"
		},

		{ GUI_SECTION,
";    renderer: What video system to use for rendering. Use the one most compatible with your system."
";              Possible values: opengl, hard2d, soft2d.\n"
";               opengl: default 3D accelerated renderer with shaders support\n"
";               accelerated: hardware accelerated renderer without shaders support\n"
";               software: software renderer without shaders support\n"
";        mode: Possible values: normal, compact, realistic.\n"
";                  normal: the system unit places itself at the bottom of the display and is always visible\n"
";                 compact: the system unit disappears when input is grabbed or CTRL-F1 is pressed\n"
";               realistic: the system is rendered in its entirety, monitor included\n"
";      keymap: Keymap table file. If none specified the default keymap.map file in the share dir will be used.\n"
";        grab: If 'no' then the mouse will not be hidden when grabbed (useful when debugging " PACKAGE_NAME ")\n"
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
";   show_leds: Show or hide the drives motor activity led at the bottom-right (useful in compact GUI mode)\n"
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
";             Possible values: auto, or a path string\n"
";              auto: the file name is automatically set depending on the system model\n"
"; image_init: Yes if you want to initialise the RTC with the values in the CMOS image\n"
"; image_save: Yes if you want to save the CMOS in the image file when the machine is powered off\n"
		},

		{ MEM_SECTION,
"; expansion: RAM module installed in the expansion slot.\n"
";            Possible values: auto, none, 512K, 2M, 4M, 6M, 8M, 16M.\n"
";             Memory configurations for PS/1 models were:\n"
";             2011: 512K on board + 512K, 2MB modules\n"
";             2121: 2M on board + 512K, 2MB, 4MB modules\n"
";             6M, 8M, and 16M modules never officialy existed.\n"
";     speed: RAM access time in nanoseconds.\n"
";            Possible values: auto, or an integer number\n"
		},

		{ VGA_SECTION,
"; Video interface card configuration:\n"
";        rom: Path to a binary ROM file to load\n"
"; ps_bit_bug: Enable the PS bit bug emulation (used by some demo, eg. Copper '92)\n"
		},

		{ DRIVES_SECTION,
";    floppy_a: The type of floppy drive A.\n"
";              Possible values: auto, none, 3.5, 5.25\n"
";    floppy_b: The type of floppy drive B.\n"
";              Possible values: auto, none, 3.5, 5.25\n"
"; fdd_latency: A multiplier for the floppy drives rotational latency.\n"
";              You can use this parameter to speed up the FDD read/write operations.\n"
";              Possible values: a real number between 0.0 (no latency) and 1.0 (normal latency.)\n"
";         hdd: The type of fixed disk drive C.\n"
";              Possible values: none, auto, ps1, ata\n"
";               none: no hard disk installed\n"
";               auto: automatically determined by the system model\n"
";                ps1: IBM's proprietary 8-bit XTA-like controller\n"
";                ata: IDE/ATA controller\n"
		},

		{ DISK_A_SECTION,
"; These options are used to insert a floppy disk at program launch.\n"
";     path: Path of a floppy image file; if the file doesn't exist a new one will be created.\n"
"; inserted: Yes if the floppy is inserted at program lauch\n"
"; readonly: Yes if the floppy image should be write protected\n"
";     type: The type of the inserted floppy.\n"
";           Possible values: auto, 1.44M, 720K, 1.2M, 360K\n"
		},

		{ DISK_B_SECTION,
"; These options are used to insert a floppy disk at program launch.\n"
";     path: Path of a floppy image file; if the file doesn't exist a new one will be created.\n"
"; inserted: Yes if the floppy is inserted at program lauch\n"
"; readonly: Yes if the floppy image should be write protected\n"
";     type: The type of the inserted floppy.\n"
";           Possible values: auto, 1.44M, 720K, 1.2M, 360K\n"
		},

		{ DISK_C_SECTION,
"; Drive C configuration:\n"
";     type: The IBM standard fixed disk type.\n"
";           Possible values:\n"
";            auto: automatically determined by the system model or the image file\n"
";              15: reserved, don't use\n"
";              35: the IBM WDL-330P 30MB disk drive used on the PS/1 2011\n"
";            1-44: other standard types (see the project page for the list of types supported by the BIOS)\n"
";              " STR(HDD_CUSTOM_DRIVE_IDX) ": custom type (specify the geometry)\n"
";          custom: same as " STR(HDD_CUSTOM_DRIVE_IDX) "\n"
";     path: Possible values: auto, or the path of the image file to mount.\n"
";           If the file doesn't exist a new one will be created.\n"
"; readonly: Yes if the disk image should be write protected (a temporary image will be used)\n"
";     save: When you restore a savestate the disk is restored as well, as a temporary read-write image.\n"
";           Set this option to 'yes' if you want to make the changes permanent at machine power off in the file specified at 'path' "
"(unless it is write-protected)\n"
"; The following parameters are used only for disk type " STR(HDD_CUSTOM_DRIVE_IDX) " (custom type):\n"
";   cylinders: Number of cylinders (max. 1024)\n"
";       heads: Number of heads (max. 16)\n"
";     sectors: Number of sectors per track (max. 63)\n"
"; Drive capacity is cylinders*heads*sectors*512, for a maximum of 528MB (504MiB)\n"
"; The following performance parameters will be used for any disk type:\n"
";    seek_max: Maximum seek time in milliseconds\n"
";    seek_trk: Track-to-track seek time in milliseconds\n"
";   rot_speed: Rotational speed in RPM (min. 3600, max. 7200)\n"
";  interleave: Interleave ratio (typically between 1 and 8)\n"
		},

		{ MIXER_SECTION,
"; prebuffer: How many milliseconds of data to prebuffer before audio starts to be emitted. A larger value might help sound stuttering, but will introduce latency.\n"
";            Possible values: any positive integer number between 10 and 1000.\n"
";      rate: Sample rate. Use the value which is more compatible with your sound card. Any emulated device with a rate different than this will be resampled.\n"
";            Possible values: 48000, 44100, 49716.\n"
";   samples: Audio samples buffer size; a larger buffer might help sound stuttering.\n"
";            Possible values: 1024, 2048, 4096, 512, 256.\n"
";    volume: Audio volume of the emulated sound cards.\n"
";            Possible values: any positive real number, 1.0 is nominal. When in realistic GUI mode it's clamped to 1.3\n"
		},
		{ PCSPEAKER_SECTION,
"; enabled: Enable PC-Speaker emulation.\n"
";    rate: Sample rate.\n"
";          Possible values: 48000, 44100, 32000, 22050, 11025.\n"
"; filters: DSP filters. Use them to emulate the response of the typical PC speaker.\n"
";          Possible values: a list of filter definitions. See the README for more info.\n"
";          Leave empty to disable\n"
";  volume: Audio volume.\n"
		},
		{ PS1AUDIO_SECTION,
"; enabled: Install the PS/1 Audio/Joystick Card.\n"
";    rate: Sample rate of the PSG (Programmable Sound Generator). The DAC rate is programmed at run-time.\n"
";          Possible values: 48000, 44100, 32000, 22050, 11025.\n"
"; filters: DSP filters, applied to both the DAC and PSG channels.\n"
";          Possible values: a list of filter definitions. See the README for more info.\n"
";  volume: Audio volume.\n"
		},
		{ ADLIB_SECTION,
"; enabled: Install the AdLib Audio Card.\n"
";    rate: Sample rate. The real AdLib uses a frequency of 49716Hz.\n"
";          Possible values: 48000, 49716, 44100, 32000, 22050, 11025.\n"
"; filters: DSP filters.\n"
";          Possible values: a list of filter definitions. See the README for more info.\n"
";  volume: Audio volume.\n"
		},
		{ SOUNDFX_SECTION,
"; Volumes are expressed as positive real numbers.\n"
";   volume: General volume of the sound effects. Set to 0.0 to disable, 1.0 for normal.\n"
"; fdd_seek: Volume of FDD seeks.\n"
"; fdd_spin: Volume of FDD spin noise.\n"
"; hdd_seek: Volume of HDD seeks.\n"
"; hdd_spin: Volume of HDD spin noise.\n"
";   system: Volume of system unit's and monitor's noises (realistic GUI mode only.)\n"
		},

		{ COM_SECTION, "" },

		{ LPT_SECTION, "" },
};

std::vector<std::pair<std::string, std::vector<std::string>>> AppConfig::ms_keys_order = {
	{ PROGRAM_SECTION, {
		PROGRAM_MEDIA_DIR,
		PROGRAM_CAPTURE_DIR,
		PROGRAM_THREADS_SYNC,
		PROGRAM_VSYNC,
		PROGRAM_FRAMECAP
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
	{ SYSTEM_SECTION, {
		SYSTEM_ROMSET,
		SYSTEM_MODEL
	} },
	{ CPU_SECTION, {
		CPU_MODEL,
		CPU_FREQUENCY
	} },
	{ MEM_SECTION, {
		MEM_RAM_EXP,
		MEM_RAM_SPEED
	} },
	{ VGA_SECTION, {
		VGA_ROM,
		VGA_PS_BIT_BUG
	} },
	{ CMOS_SECTION, {
		CMOS_IMAGE_FILE,
		CMOS_IMAGE_RTC_INIT,
		CMOS_IMAGE_SAVE
	} },
	{ DRIVES_SECTION, {
		DRIVES_FDD_A,
		DRIVES_FDD_B,
		DRIVES_FDD_LAT,
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
		DISK_TYPE,
		DISK_PATH,
		DISK_READONLY,
		DISK_SAVE,
		DISK_CYLINDERS,
		DISK_HEADS,
		DISK_SPT,
		DISK_SEEK_MAX,
		DISK_SEEK_TRK,
		DISK_ROT_SPEED,
		DISK_INTERLEAVE
	} },
	{ MIXER_SECTION, {
		MIXER_PREBUFFER,
		MIXER_RATE,
		MIXER_SAMPLES,
		MIXER_VOLUME
	} },
	{ PCSPEAKER_SECTION, {
		PCSPEAKER_ENABLED,
		PCSPEAKER_RATE,
		PCSPEAKER_FILTERS,
		PCSPEAKER_VOLUME
	} },
	{ PS1AUDIO_SECTION, {
		PS1AUDIO_ENABLED,
		PS1AUDIO_RATE,
		PS1AUDIO_FILTERS,
		PS1AUDIO_VOLUME
	} },
	{ ADLIB_SECTION, {
		ADLIB_ENABLED,
		ADLIB_RATE,
		ADLIB_FILTERS,
		ADLIB_VOLUME
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

void AppConfig::merge(const AppConfig &_other, ConfigType _type)
{
	for(auto othersec : _other.m_values) {
		auto secname = othersec.first;
		if(_type != ANY_CONFIG && ms_def_values[_type].find(secname) == ms_def_values[_type].end()) {
			PDEBUGF(LOG_V2, LOG_PROGRAM, "ignoring ini section [%s]\n", secname.c_str());
			continue;
		}
		for(auto otherentry : othersec.second) {
			auto key = otherentry.first;
			if(_type != ANY_CONFIG && ms_def_values[_type][secname].find(key) == ms_def_values[_type][secname].end()) {
				PDEBUGF(LOG_V2, LOG_PROGRAM, "ignoring ini key [%s]:%s\n", secname.c_str(), key.c_str());
				continue;
			}
			// it will insert a new section in the current values if not present
			m_values[secname][key] = otherentry.second;
		}
	}
}

void AppConfig::copy(const AppConfig &_config)
{
	(*this) = _config;
}

int AppConfig::parse_int(const string &_str)
{
	const char* value = _str.c_str();
	char* end;
	// This parses "1234" (decimal) and also "0x4D2" (hex)
	int n = strtol(value, &end, 0);
	if(end <= value) {
		PDEBUGF(LOG_V1, LOG_PROGRAM, "'%s' is not an integer\n", value);
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
		PDEBUGF(LOG_V1, LOG_PROGRAM, "'%s' is not a real\n", value);
		throw std::exception();
	}
	return n;
}

bool AppConfig::parse_bool(string _str)
{
	// Convert to lower case to make string comparisons case-insensitive
	_str = str_to_lower(_str);
	if (_str == "true" || _str == "yes" || _str == "on" || _str == "1") {
		return true;
	} else if (_str == "false" || _str == "no" || _str == "off" || _str == "0") {
		return false;
	} else {
		PDEBUGF(LOG_V1, LOG_PROGRAM, "'%s' is not a boolean\n", _str.c_str());
		throw std::exception();
	}
}

std::vector<string> AppConfig::parse_tokens(string _str, string _regex_sep)
{
	return str_parse_tokens(_str, _regex_sep);
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

string AppConfig::get_value(ini_file_t &_values, const string &section, const string &name)
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

string AppConfig::get_value(const string &section, const string &name)
{
	string valstr;
	try {
		valstr = get_value(m_values, section, name);
	} catch(std::exception &) {
		try {
			valstr = get_value(ms_def_values[PROGRAM_CONFIG], section, name);
		} catch(std::exception &) {
			try {
				valstr = get_value(ms_def_values[MACHINE_CONFIG], section, name);
			} catch(std::exception &) {
				PERRF(LOG_PROGRAM, "[%s]:%s is not a valid configuration key!\n", section.c_str(),name.c_str());
				throw;
			}
		}
		PWARNF(LOG_PROGRAM, "[%s]:%s undefined, loading default: '%s'\n", section.c_str(),name.c_str(), valstr.c_str());
	}
	return valstr;
}

int AppConfig::try_int(const string &_section, const string &_name)
{
	string valstr = get_value(_section, _name);
	int value = parse_int(valstr);
	return value;
}

int AppConfig::get_int(const string &_section, const string &_name)
{
	int value;
	try {
		value = try_int(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get integer value for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return value;
}

int AppConfig::get_int(const string &_section, const string &_name, int _default)
{
	try {
		return try_int(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

void AppConfig::set_int(const string &_section, const string &_name, int _value)
{
	m_values[make_key(_section)][make_key(_name)] = std::to_string(_value);
}

double AppConfig::try_real(const string &_section, const string &_name)
{
	string valstr = get_value(_section, _name);
	double value = parse_real(valstr);
	return value;
}

double AppConfig::get_real(const string &_section, const string &_name)
{
	double value = 0.0;
	try {
		value = try_real(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get real value for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return value;
}

double AppConfig::get_real(const string &_section, const string &_name, double _default)
{
	try {
		return try_real(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

void AppConfig::set_real(const string &_section, const string &_name, double _value)
{
	m_values[make_key(_section)][make_key(_name)] = std::to_string(_value);
}

bool AppConfig::try_bool(const string &_section, const string &_name)
{
	string valstr = get_value(_section, _name);
	bool value = parse_bool(valstr);
	return value;
}

bool AppConfig::get_bool(const string &_section, const string &_name)
{
	bool value;
	try {
		value = try_bool(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get bool value for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return value;
}

bool AppConfig::get_bool(const string &_section, const string &_name, bool _default)
{
	try {
		return try_bool(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

void AppConfig::set_bool(const string &_section, const string &_name, bool _value)
{
	m_values[make_key(_section)][make_key(_name)] = _value?"yes":"no";
}

string AppConfig::get_string(const string &_section, const string &_name)
{
	string val;
	try {
		val = get_value(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}
	return val;
}

string AppConfig::get_string(const string &_section, const string &_name, const string &_default)
{
	try {
		return get_value(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
}

string AppConfig::get_string(const string &_section, const string &_name,
		const std::set<string> _allowed,
		const string &_default)
{
	string value;
	try {
		value = get_value(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
	if(_allowed.find(value) == _allowed.end()) {
		return _default;
	}
	return value;
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
		filename = get_value(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "unable to get string [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
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

string AppConfig::find_media(const string &_filename)
{
	string path = get_file_path(_filename, FILE_TYPE_MEDIA);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file_path(_filename, FILE_TYPE_USER);
	}
	return path;
}

unsigned AppConfig::get_enum(const string &_section, const string &_name, const ini_enum_map_t &_enum_map)
{
	string enumstr;
	try {
		enumstr = get_value(_section, _name);
	} catch(std::exception &e) {
		PERRF(LOG_PROGRAM, "Unable to get string for [%s]:%s\n", _section.c_str(), _name.c_str());
		throw;
	}

	auto enumvalue = _enum_map.find(enumstr);
	if(enumvalue == _enum_map.end()) {
		PERRF(LOG_PROGRAM, "Invalid value '%s' for [%s]:%s\n",
				enumstr.c_str(), _section.c_str(), _name.c_str());
		throw std::exception();
	}
	return enumvalue->second;
}

unsigned AppConfig::get_enum(const string &_section, const string &_name,
		const ini_enum_map_t &_enum_map, unsigned _default)
{
	string enumstr;
	try {
		enumstr = get_value(_section, _name);
	} catch(std::exception &e) {
		return _default;
	}
	auto enumvalue = _enum_map.find(enumstr);
	if(enumvalue == _enum_map.end()) {
		return _default;
	}
	return enumvalue->second;
}

unsigned AppConfig::get_enum_quiet(const string &_section, const string &_name,
		const ini_enum_map_t &_enum_map)
{
	string enumstr = get_value(_section, _name);
	auto enumvalue = _enum_map.find(enumstr);
	if(enumvalue == _enum_map.end()) {
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
		PERRF(LOG_FS,"Cannot open '%s' for writing\n",_filename.c_str());
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
			file << key << "=" << get_value(section.first, key) << std::endl;
		}
		file << std::endl;
	}

	file.close();
}

