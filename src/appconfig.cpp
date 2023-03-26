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

#include "ibmulator.h"
#include "utils.h"
#include "filesys.h"
#include "appconfig.h"
#include "program.h"
#include <algorithm>
#include <regex>
#include <cctype>
#include <cstdlib>


ini_file_t AppConfig::ms_def_values[2] = {

// PROGRAM_CONFIG
{
	{ PROGRAM_SECTION, {
		{ PROGRAM_MEDIA_DIR,    ""       },
		{ PROGRAM_WAIT_METHOD,  "auto"   },
		{ PROGRAM_LOG_FILE,     "log.txt"}
	} },

	{ GUI_SECTION, {
		{ GUI_RENDERER,           "opengl"     },
		{ GUI_FRAMECAP,           "vga"        },
		{ GUI_KEYMAP,             "keymap.map" },
		{ GUI_MOUSE_GRAB,         "yes"        },
		{ GUI_MOUSE_ACCELERATION, "no"         },
		{ GUI_SCREEN_DPI,         "96"         },
		{ GUI_WIDTH,              "640"        },
		{ GUI_HEIGHT,             "480"        },
		{ GUI_FULLSCREEN,         "no"         },
		{ GUI_SHOW_INDICATORS,    "no"         },
		{ GUI_MODE,               "normal"     },
		{ GUI_COMPACT_TIMEOUT,    "1.5"        },
		{ GUI_REALISTIC_ZOOM,     "cycle"      },
		{ GUI_REALISTIC_STYLE,    "bright"     },
		{ GUI_BG_R,               "0"          },
		{ GUI_BG_G,               "0"          },
		{ GUI_BG_B,               "0"          },
		{ GUI_UI_SCALING,         "100%"       }
	} },

	{ DIALOGS_SECTION, {
		{ DIALOGS_FILE_TYPE,  "custom" },
		{ DIALOGS_FILE_MODE,  "grid"   },
		{ DIALOGS_FILE_ORDER, "name"   },
		{ DIALOGS_FILE_ZOOM,  "2"      },
		{ DIALOGS_SAVE_MODE,  "grid"   },
		{ DIALOGS_SAVE_ORDER, "date"   },
		{ DIALOGS_SAVE_ZOOM,  "1"      }
	} },

	{ CAPTURE_SECTION, {
		{ CAPTURE_DIR,           ""     },
		{ CAPTURE_VIDEO_MODE,    "avi"  },
		{ CAPTURE_VIDEO_FORMAT,  "zmbv" },
		{ CAPTURE_VIDEO_QUALITY, "3"    }
	} },

	{ DISPLAY_SECTION, {
		{ DISPLAY_NORMAL_SCALE,     "fill" },
		{ DISPLAY_NORMAL_ASPECT,    "4:3"  },
		{ DISPLAY_NORMAL_SHADER,    "normal_mode/stock.slangp" },
		{ DISPLAY_REALISTIC_SHADER, "realistic_mode/stock.slangp" },
		{ DISPLAY_BRIGHTNESS,       "1.0" },
		{ DISPLAY_CONTRAST,         "1.0" },
		{ DISPLAY_SATURATION,       "1.0" },
		{ DISPLAY_AMBIENT,          "1.0" },
		{ DISPLAY_SAMPLERS_MODE,    "auto" },
		{ DISPLAY_SHADER_INPUT,     "auto" },
		{ DISPLAY_SHADER_OUTPUT,    "native" },
		{ DISPLAY_FILTER,           "bilinear" }
	} },

	{ CPU_SECTION, {
		{ CPU_HLT_WAIT, "500" }
	} },

	{ CMOS_SECTION, {
		{ CMOS_IMAGE_RTC_INIT, "no"  },
		{ CMOS_IMAGE_SAVE,     "yes" }
	} },

	{ DRIVES_SECTION, {
		{ DRIVES_FLOPPY_COMMIT, "yes" },
		{ DRIVES_HDD_COMMIT,    "yes" }
	} },

	{ MIXER_SECTION, {
		{ MIXER_RATE,      "48000" },
		{ MIXER_SAMPLES,   "1024"  },
		{ MIXER_PREBUFFER, "50"    },
		{ MIXER_VOLUME,    "1.0"   }
	} },

	{ MIDI_SECTION, {
		{ MIDI_ENABLED,      "yes"   },
		{ MIDI_DEVICE,       ""      },
		{ MIDI_DEVTYPE,      ""      },
		{ MIDI_DELAY,        "auto"  }
	} },
	
	{ PCSPEAKER_SECTION, {
		{ PCSPEAKER_RATE,    "48000" },
		{ PCSPEAKER_FILTERS, ""      },
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

	{ SBLASTER_SECTION, {
		{ SBLASTER_DAC_FILTERS, ""      },
		{ SBLASTER_DAC_VOLUME,  "1.0"   },
		{ SBLASTER_OPL_RATE,    "48000" },
		{ SBLASTER_OPL_FILTERS, ""      },
		{ SBLASTER_OPL_VOLUME,  "1.4"   }
	} },
	
	{ GAMEPORT_SECTION, {
		{ GAMEPORT_ENABLED,  "yes" }
	} },
	
	{ SOUNDFX_SECTION, {
		{ SOUNDFX_ENABLED,  "yes" },
		{ SOUNDFX_VOLUME,   "1.0" },
		{ SOUNDFX_FDD_SPIN, "0.4" },
		{ SOUNDFX_FDD_SEEK, "0.4" },
		{ SOUNDFX_HDD_SPIN, "0.4" },
		{ SOUNDFX_HDD_SEEK, "0.4" },
		{ SOUNDFX_SYSTEM,   "1.0" },
		{ SOUNDFX_MODEM,    "1.2" },
		{ SOUNDFX_MODEM_FILTERS, "auto" },
		{ SOUNDFX_MODEM_COUNTRY, "eu" }
	} },

	{ MODEM_SECTION, {
		{ MODEM_BAUD_RATE,    "2400"      },
		{ MODEM_LISTEN_ADDR,  ""          },
		{ MODEM_PHONEBOOK,    "phone.txt" },
		{ MODEM_TELNET_MODE,  "no"        },
		{ MODEM_CONN_TIMEOUT, "10"        },
		{ MODEM_WARM_DELAY,   "no"        },
		{ MODEM_CONNECT_CODE, "auto"      },
		{ MODEM_ECHO_ON,      "yes"       },
		{ MODEM_HANDSHAKE,    "no"        },
		{ MODEM_DUMP,         ""          }
	} },

	{ SERIAL_SECTION, {
		{ SERIAL_A_DUMP, "" },
		{ SERIAL_B_DUMP, "" },
		{ SERIAL_C_DUMP, "" },
		{ SERIAL_D_DUMP, "" }
	} },

	{ LPT_SECTION, {
		{ LPT_PORT,    "LPT1" },
		{ LPT_FILE,    ""     }
	} },

	{ PRN_SECTION, {
		{ PRN_CONNECTED,     "no"     },
		{ PRN_MODE,          "epson"  },
		{ PRN_COLOR,         "no"     },
		{ PRN_INK,           "medium" },
		{ PRN_PREVIEW_DIV,   "low"    },
		{ PRN_EPSON_CSET,    "basic"  },
		{ PRN_IBM_CSET,      "intl1"  },
		{ PRN_PAPER_SIZE,    "letter" },
		{ PRN_PAPER_TYPE,    "forms"  },
		{ PRN_BOF,           "0"      },
		{ PRN_TOP_MARGIN,    "auto"   },
		{ PRN_BOTTOM_MARGIN, "0.1875" },
		{ PRN_SHOW_HEAD,     "yes"    }
	} },

	{ LOG_SECTION, {
		{ LOG_OVERRIDE_VERBOSITY, "no"},
		{ LOG_PROGRAM_VERBOSITY,  "0" },
		{ LOG_FS_VERBOSITY,       "0" },
		{ LOG_GFX_VERBOSITY,      "0" },
		{ LOG_INPUT_VERBOSITY,    "0" },
		{ LOG_GUI_VERBOSITY,      "0" },
		{ LOG_MACHINE_VERBOSITY,  "0" },
		{ LOG_MIXER_VERBOSITY,    "0" },
		{ LOG_MEM_VERBOSITY,      "0" },
		{ LOG_CPU_VERBOSITY,      "0" },
		{ LOG_MMU_VERBOSITY,      "0" },
		{ LOG_PIT_VERBOSITY,      "0" },
		{ LOG_PIC_VERBOSITY,      "0" },
		{ LOG_DMA_VERBOSITY,      "0" },
		{ LOG_KEYB_VERBOSITY,     "0" },
		{ LOG_VGA_VERBOSITY,      "0" },
		{ LOG_CMOS_VERBOSITY,     "0" },
		{ LOG_FDC_VERBOSITY,      "0" },
		{ LOG_HDD_VERBOSITY,      "0" },
		{ LOG_AUDIO_VERBOSITY,    "0" },
		{ LOG_GAMEPORT_VERBOSITY, "0" },
		{ LOG_LPT_VERBOSITY,      "0" },
		{ LOG_PRN_VERBOSITY,      "0" },
		{ LOG_COM_VERBOSITY,      "0" },
		{ LOG_MIDI_VERBOSITY,     "0" }
	} },
},

//MACHINE_CONFIG
{
	{ GUI_SECTION, {
		{ GUI_MOUSE_TYPE, "ps2" },
	} },
	
	{ DISPLAY_SECTION, {
		{ DISPLAY_TYPE, "color" }
	} },
	
	{ SYSTEM_SECTION, {
		{ SYSTEM_ROMSET,       ""     },
		{ SYSTEM_MODEL,        "auto" },
		{ SYSTEM_BIOS_PATCHES, ""     }
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
		{ DRIVES_FDD_A,    "auto" },
		{ DRIVES_FDD_B,    "auto" },
		{ DRIVES_FDC_TYPE, "raw"  },
		{ DRIVES_FDC_MODE, "auto" },
		{ DRIVES_FDC_OVR,  "250"  },
		{ DRIVES_FDD_LAT,  "1.0"  },
		{ DRIVES_HDC_TYPE, "auto" }
	} },

	{ DISK_A_SECTION, {
		{ DISK_INSERTED,  "no"   },
		{ DISK_READONLY,  "no"   },
		{ DISK_PATH,      ""     },
		{ DISK_TYPE,      "auto" },
		{ DISK_CYLINDERS, "auto" },
		{ DISK_HEADS,     "auto" }
	} },

	{ DISK_B_SECTION, {
		{ DISK_INSERTED,  "no"   },
		{ DISK_READONLY,  "no"   },
		{ DISK_PATH,      ""     },
		{ DISK_TYPE,      "auto" },
		{ DISK_CYLINDERS, "auto" },
		{ DISK_HEADS,     "auto" }
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

	{ SBLASTER_SECTION, {
		{ SBLASTER_ENABLED, "no"    },
		{ SBLASTER_MODEL,   "sb2"   },
		{ SBLASTER_IOBASE,  "0x220" },
		{ SBLASTER_DMA,     "1"     },
		{ SBLASTER_IRQ,     "7"     }
	} },
	
	{ MPU401_SECTION, {
		{ MPU401_ENABLED,  "no"          },
		{ MPU401_IOBASE,   "0x330"       },
		{ MPU401_IRQ,      "3"           },
		{ MPU401_MODE,     "intelligent" }
	} },
	
	{ SERIAL_SECTION, {
		{ SERIAL_ENABLED,       "yes"  },
		{ SERIAL_A_MODE,        "auto" },
		{ SERIAL_A_DEV,         ""     },
		{ SERIAL_A_TX_DELAY,    "20"   },
		{ SERIAL_A_TCP_NODELAY, "yes"  },
		{ SERIAL_B_MODE,        "auto" },
		{ SERIAL_B_DEV,         ""     },
		{ SERIAL_B_TX_DELAY,    "20"   },
		{ SERIAL_B_TCP_NODELAY, "yes"  },
		{ SERIAL_C_MODE,        "auto" },
		{ SERIAL_C_DEV,         ""     },
		{ SERIAL_C_TX_DELAY,    "20"   },
		{ SERIAL_C_TCP_NODELAY, "yes"  },
		{ SERIAL_D_MODE,        "auto" },
		{ SERIAL_D_DEV,         ""     },
		{ SERIAL_D_TX_DELAY,    "20"   },
		{ SERIAL_D_TCP_NODELAY, "yes"  }
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
"; media_dir: The default directory used to search for disk images.\n"
";  log_file: path of the log file\n"
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
";  hlt_wait: Interrupts polling time in nanoseconds when the CPU enters an HALT state.\n"
";            Affects responsiveness in guest operating systems that use the HLT instruction for idling.\n"
		},

		{ GUI_SECTION,
";       renderer: What video system to use for rendering. Use the one most compatible with your system.\n"
";                 Possible values: opengl, accelerated, software.\n"
";                       opengl: default 3D accelerated renderer with shaders support\n"
";                  accelerated: hardware accelerated renderer without shaders support\n"
";                     software: software renderer without shaders support\n"
";       framecap: Method to use to limit the number of displayed frames per second.\n"
";                 Possible values:\n"
";                    vga: limited by the emulated VGA frequency\n"
";                  vsync: limited by the host monitor frequency (incompatible with the software renderer)\n"
";                     no: no limits\n"
";           mode: Possible values: normal, compact, realistic.\n"
";                     normal: the system unit places itself at the bottom of the display and is always visible\n"
";                    compact: the system unit disappears when input is grabbed or CTRL+F1 is pressed\n"
";                  realistic: the system is rendered in its entirety, monitor included\n"
";compact_timeout: Number of seconds before hiding the interface in compact mode. 0 to disable.\n"
"; realistic_zoom: Zoom level to activate when CTRL+F1 is pressed in realistic mode.\n"
";                 Possible values: cycle, monitor, bezel, screen.\n"
";realistic_style: Initial interface style when in realistic mode.\n"
";                 Possible values: bright, dark.\n"
";         keymap: Keymap table file(s). Separate multiple file names with '|'.\n"
";                 If none specified the default keymap.map file in the 'share' directory will be used.\n"
";          mouse: Mouse type.\n"
";                 Possible values: none, ps2, serial\n"
";    mouse_accel: Enable mouse acceleration\n"
";          width: Window width in pixel.\n"
";         height: Window height in pixel (for normal GUI mode it doesn't include the system unit.)\n"
";     fullscreen: Start directly in fullscreen.\n"
";            dpi: Resolution of the host display in DPI (currently used only for mouse acceleration).\n"
";         bg_XXX: Background window color\n"
";show_indicators: Show status indicators on the top-right corner of the screen.\n"
";     ui_scaling: UI scaling, between 100% and 500% with increments every 25% (125%, 150%, 175%, ...)\n"
		},

		{ DIALOGS_SECTION,
";  file_type: The type of file select dialogs to use:\n"
";              custom: use the " PACKAGE_NAME "'s custom implementation\n"
";              native: use your OS native file dialogs (won't play nice when in fullscreen mode)\n"
";  file_mode: Custom file dialogs initial view mode: grid, list\n"
"; file_order: Custom file dialogs initial order: name, date\n"
";  file_zoom: Custom file dialogs initial zoom level: 0, 1, 2, 3, 4\n"
";  save_mode: Savestate dialogs initial view mode: grid, list\n"
"; save_order: Savestate dialogs initial order: date, title, slot\n"
";  save_zoom: Savestate dialogs initial zoom level: 0, 1, 2\n"
		},

		{ CAPTURE_SECTION,
";     directory: Directory where things like video, audio, savestates, and screenshots get captured.\n"
";  video_format: Format to use for video recordings.\n"
";                Possible values: zmbv, mpng\n"
";                 zmbv: DOSBox Capture Codec\n"
";                 mpng: Motion PNG\n"
"; video_quality: Compression level of the video stream (higher levels have very high load on the CPU).\n"
";                Possible values: integer between 0 (uncompressed) and 9 (max compression).\n"
		},

		{ DISPLAY_SECTION,
";               type: Possible values: color, monochrome.\n"
";       normal_scale: The viewport's scaling mode (normal/compact GUI modes).\n"
";                     Possible values: fill, integer.\n"
";                         fill: scale to fill the available area.\n"
";                      integer: scale only at integer multiples.\n"
";      normal_aspect: Aspect ratio of the viewport (normal/compact GUI modes).\n"
";                     Possible values: W:H, vga, area, original.\n"
";                           W:H: use the specified aspect ratio (eg. 4:3, 16:10, ...).\n"
";                           vga: maintain the current VGA video mode ratio.\n"
";                          area: same as the available viewing area.\n"
";                      original: same as 4:3.\n"
";      normal_shader: Shader to use for rendering in normal and compact GUI modes (OpenGL renderer only).\n"
";   realistic_shader: Shader to use for rendering in realistic GUI mode (OpenGL renderer only).\n"
";         brightness: Monitor brightness, as a number from 0.0 to 1.0 (needs support by the shader).\n"
";           contrast: Monitor contrast, as a number from 0.0 to 1.0 (needs support by the shader).\n"
";         saturation: Monitor saturation, as a number from 0.0 to 1.0 (needs support by the shader).\n"
";      ambient_light: Intensity of the ambient light, as a number from 0.0 to 1.0 (needs support by the shader).\n"
";  shader_input_size: Shader's input texture size.\n"
";                     Possible values: auto, crtc, video_mode.\n"
";                            auto: as defined by the shader (default is video_mode).\n"
";                            crtc: same as the video card's CRTC output.\n"
";                      video_mode: same as the current video mode.\n"
"; shader_output_size: Shader's rendering resolution.\n" 
";                     Possible values: native, WxH, max_WxH.\n"
";                       native: use the native resolution of your monitor.\n"
";                          WxH: a specific size in pixels, maintaining the viewport's aspect ratio (the final stretching will use `upscaling_filter`).\n"
";                      max_WxH: a maximum size in pixels, maintaining the viewport's aspect ratio (the final stretching will use `upscaling_filter`).\n"
";   upscaling_filter: Stretching filter for the accelerated renderer or for the last pass of OpenGL shaders.\n"
";                     For OpenGL shaders, it will be used only if the last pass does not render directly to the backbuffer.\n"
";                     Possible values: nearest, bilinear, bicubic\n"
";                     The bicubic filter is supported only by the OpenGL renderer.\n"
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
";             6M, 8M, and 16M modules never officially existed.\n"
";     speed: RAM access time in nanoseconds.\n"
";            Possible values: auto, or an integer number\n"
		},

		{ VGA_SECTION,
"; Video interface card configuration:\n"
";        rom: Path to a binary ROM file to load.\n"
";              The PS/1 BIOS won't use it but other BIOSes might require it.\n"
"; ps_bit_bug: Enable the Palette Size (PS) bit bug emulation.\n"
";              This VGA Attribute Controller's bit is not used in 256-color mode, except in the ET4000AX rev. TC6058AF\n"
";              (req. by Copper '92 demo).\n"
		},

		{ DRIVES_SECTION,
";      floppy_a: The type of floppy drive A.\n"
";                Possible values: auto, none, 3.5, 5.25, 5.25 DD\n"
";                 3.5: install a 3.5\" HD 1.44M drive\n"
";                 5.25: install a 5.25\" HD 1.2M drive\n"
";                 5.25 DD: install a 5.25\" DD 360K drive\n"
";      floppy_b: The type of floppy drive B.\n"
";                Possible values: auto, none, 3.5, 5.25, 5.25 DD\n"
"; floppy_commit: Commit floppy images to storage when ejected?\n"
";                Possible values: yes, no, ask, discard_states\n"
";                 yes: commit data\n"
";                  no: discard written data (beware: data loss)\n"
";                 ask: a message box will be shown to ask what to do\n"
";                 discard_states: discard data if current state is from a savestate, otherwise commit\n"
";      fdc_type: Type of the floppy disk controller emulation.\n"
";                Possible values: raw, flux\n"
";                  raw: raw sector floppy images only (fastest)\n"
";                 flux: floppy disk flux-based emulation (most realistic)\n"
";      fdc_mode: Mode of operation of the floppy disk controller.\n"
";                Possible values: auto, pc-at, model30\n"
";   fdd_latency: A multiplier for the floppy drives rotational latency (only for the \"raw\" controller type).\n"
";                You can use this parameter to speed up the FDD read/write operations.\n"
";                Possible values: a real number between 0.0 (no latency) and 1.0 (normal latency.)\n"
";      hdc_type: The type of hard disk controller.\n"
";                Possible values: none, auto, ps1, ata\n"
";                 none: no hard disk installed\n"
";                 auto: automatically determined by the system model\n"
";                  ps1: IBM's proprietary 8-bit XTA-like controller\n"
";                  ata: IDE/ATA controller\n"
";    hdd_commit: Commit hard disk images when program closes or a new state is loaded?\n"
";                Possible values: yes, no, ask, discard_states\n"
";                 yes: commit data when program closes or new state gets loaded\n"
";                  no: discard written data (beware: data loss)\n"
";                 ask: a message box will be shown to ask what to do\n"
";                 discard_states: discard data if current state is from a savestate, otherwise commit\n"
		},

		{ DISK_A_SECTION,
"; These options are used to insert a floppy disk at program launch.\n"
";     path: Path of a floppy image file; if the file doesn't exist a new one will be created.\n"
"; inserted: Yes if the floppy is inserted at program lauch\n"
"; readonly: Yes if the floppy image should be write protected\n"
		},

		{ DISK_B_SECTION,
"; These options are used to insert a floppy disk at program launch.\n"
";     path: Path of a floppy image file; if the file doesn't exist a new one will be created.\n"
"; inserted: Yes if the floppy is inserted at program lauch\n"
"; readonly: Yes if the floppy image should be write protected\n"
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
		{ MIDI_SECTION,
";     enabled: Enable MIDI output.\n"
";      device: MIDI device to use. See the README for more info.\n"
"; sysex_delay: Minimum amount of delay in milliseconds for SysEx messages. See the README for more info.\n"
";              Possible values: auto, or an integer number (0 to disable all delays).\n"
		},
		{ PCSPEAKER_SECTION,
"; enabled: Enable PC-Speaker emulation.\n"
";    rate: Sample rate.\n"
";          Possible values: 48000, 44100, 32000, 22050, 11025.\n"
"; filters: DSP filters. Use them to emulate the response of the typical PC speaker.\n"
";          Possible values: a list of filter definitions. See the README for more info.\n"
";          Example: LowPass,order=5,cutoff=5000|HighPass,order=5,cutoff=500\n"
";  volume: Audio volume.\n"
		},
		{ PS1AUDIO_SECTION,
"; enabled: Install the PS/1 Audio/Joystick Card.\n"
";    rate: Sample rate of the PSG (Programmable Sound Generator). The DAC rate is programmed at run-time.\n"
";          Possible values: 48000, 44100, 32000, 22050, 11025.\n"
"; filters: DSP filters, applied to both the DAC and PSG channels.\n"
";  volume: Audio volume.\n"
		},
		{ ADLIB_SECTION,
"; enabled: Install the AdLib Audio Card (cannot be installed with Sound Blaster).\n"
";    rate: Sample rate. The real AdLib uses a frequency of 49716Hz.\n"
";          Possible values: 48000, 49716, 44100, 32000, 22050, 11025.\n"
"; filters: DSP filters.\n"
";  volume: Audio volume.\n"
		},
		{ SBLASTER_SECTION,
";     enabled: Install the Sound Blaster 2.0 audio card.\n"
";      iobase: The I/O base address, as an hexadecimal number.\n"
";              Possible values: 0x220, 0x240.\n"
";         irq: The IRQ line number.\n"
";              Possible values: 3, 5, 7.\n"
";         dma: The DMA channel number.\n"
";              Possible values: 0, 1, 3.\n"
"; dac_filters: Audio filters for the DAC\n"
";              Possible values: a list of filter definitions. See the README for more info.\n"
";  dac_volume: DAC's audio volume.\n"
";    opl_rate: OPL chip's sample rate. The real hardware uses a frequency of 49716Hz.\n"
";              Possible values: 48000, 49716, 44100, 32000, 22050, 11025.\n"
"; opl_filters: Audio filters for the OPL chip\n"
";              Possible values: a list of filter definitions. See the README for more info.\n"
";  opl_volume: OPL chip's audio volume.\n"
		},
		{ GAMEPORT_SECTION, 
"; enabled: Install the Game Port.\n"
		},
		{ MPU401_SECTION,
"; enabled: Install the MPU-401 interface card.\n"
";  iobase: The I/O base address, as an hexadecimal number.\n"
";          Possible values: 0x330, 0x300\n"
";     irq: The IRQ line number.\n"
";          Possible values: 3, 5, 7\n"
";    mode: Mode of operation.\n"
";          Possible values: intelligent, uart.\n"
		},
		{ SOUNDFX_SECTION,
"; Volumes are expressed as positive real numbers.\n"
";         enabled: Enable sound effects emulation.\n"
";          volume: General volume of the sound effects.\n"
";        fdd_seek: Volume of FDD seeks.\n"
";        fdd_spin: Volume of FDD spin noise.\n"
";        hdd_seek: Volume of HDD seeks.\n"
";        hdd_spin: Volume of HDD spin noise.\n"
";          system: Volume of system unit's and monitor's noises (realistic GUI mode only.)\n"
";           modem: Volume of the serial modem's internal speaker.\n"
";   modem_country: Determines the style of the modem tones.\n"
";                  Possible values: eu, us.\n"
		},

		{ SERIAL_SECTION,
";     enabled: Install the Serial Port.\n"
";        mode: Mode of operation.\n"
";              Possible values:\n"
";               auto: let the system decide (depends on the [gui]:mouse setting and restored states)\n"
";               dummy: no input/output, dummy serial device attached\n" 
";               file: write output to a file\n"
";               term: terminal connection (Linux only)\n"
";               net-server: network server that accepts incoming connections\n"
";               net-client: network client for connecting to a network server\n"
";               modem: virtual modem that connects and receives calls over the network\n"
";         dev: address or path of the attached serial device (see the README).\n"
";    tx_delay: wait time for network data transmissions in milliseconds (network modes only).\n"
"; tcp_nodelay: use the TCP_NODELAY socket option (network modes only).\n"
		},

		{ MODEM_SECTION,
";      baud_rate: Speed of the modem in bits-per-second.\n"
";                 Possible values: 300, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 33600, 56000.\n" 
";    listen_addr: Address and port on which to open a listening socket for incoming calls.\n"
"; phonebook_file: The file containing the mapping between phone numbers and network addresses.\n"
";    telnet_mode: Enable Telnet protocol interpretation (needed when connecting to BBSes behind Telnet servers).\n"
";   conn_timeout: Connection timeout in seconds when connecting to a remote host.\n"
";   warmup_delay: Drop all incoming and outgoing traffic for a short period after answering a call.\n"
";   connect_code: Numeric result code after a successful connect.\n"
";                 Possible values: auto, or an integer number.\n"
";                  auto: the modem will report a code that depends on the baud_rate value.\n"
";        echo_on: Echo is enabled at power-on and after a reset.\n"
";      handshake: Pretend handshaking happens after establishing a connection. If sound effects are enabled a sound will be played.\n"
";                  Possible values: no, short, full.\n"
";                   short: handshake duration is 2 seconds, regardless of the baud rate.\n" 
";                    full: handshake duration depends on the baud rate (warning: might cause connection timeouts).\n"
		},

		{ LPT_SECTION, ""
"; enabled: Install the Parallel Port.\n"
";    port: Sets the port mapping at power on. Be aware though that the PS/1 BIOS sets the mapping according to the saved CMOS setting.\n"
";          Possible values:\n"
";           LPT1: port 3BC, IRQ 7\n"
";           LPT2: port 378, IRQ 7\n"
";           LPT3: port 278, IRQ 5\n"
";    file: Save the data sent to the port to the specified file.\n" 
		},

		{ PRN_SECTION,
"; Virtual printer configuration.\n"
";      connected: Connect the printer to the parallel port?\n"
";    interpreter: Mode of operation of the printer interpreter.\n"
";                 Possible values:\n"
";                  epson: Epson ESC/P (FX-80/JX-80)\n"
";                  ibmpp: IBM Proprinter\n"
";                  ibmgp: IBM Graphics Printer\n"
";          color: Enable color support.\n"
";            ink: How strong is the pin impact on the paper.\n"
";                 Possible values: low, medium, high\n"
";        preview: The quality of the page preview image\n"
";                 Possible values: low, high, max\n"
";  epson_charset: Charset to use for Epson emulation.\n"
";                 Possible values: basic, usa, france, germany, england, denmark1, sweden, italy, spain, japan, norway, denmark2\n"
";   ibm_charset2: Charset to use for Table2 when using IBM emulation.\n"
";                 Possible values: intl1, intl2, israel, greece, portugal, spain\n"
";     paper_size: Possible values: Letter, A4, Legal\n"
";     paper_type: Possible values: single, continuous\n"
";                  single: single sheet of paper (new sheets are automatically loaded)\n"
";                   forms: continuous forms\n"
"; bottom_of_form: bottom-of-form value at power on in PICA lines (a.k.a. skip perforation).\n"
";                 Possible values: a positive integer number\n" 
";     top_margin: distance in inches of the printing head from the top of page when the paper is loaded.\n"
";                 Possible values: a positive real number or auto\n"
";                  auto: value depends on the type of loaded paper, 0.0 for continuous forms, 0.375 (3/8\") for single sheets\n"
";  bottom_margin: distance in inches from the bottom of the page that cannot be printed (used with single sheets only).\n"
";                 Possible values: a positive real number\n"
";  show_head_pos: Show the current position of the printing head on the page preview image?\n"
		}
};

ini_order_t AppConfig::ms_keys_order = {
	{ PROGRAM_SECTION, {
		// key name,         hidden?
		{ PROGRAM_MEDIA_DIR, false },
		{ PROGRAM_LOG_FILE,  false }
	} },
	{ GUI_SECTION, {
		{ GUI_RENDERER,           false },
		{ GUI_FRAMECAP,           false },
		{ GUI_MODE,               false },
		{ GUI_COMPACT_TIMEOUT,    false },
		{ GUI_REALISTIC_ZOOM,     false },
		{ GUI_REALISTIC_STYLE,    false },
		{ GUI_KEYMAP,             false },
		{ GUI_MOUSE_TYPE,         false },
		{ GUI_MOUSE_ACCELERATION, false },
		{ GUI_WIDTH,              false },
		{ GUI_HEIGHT,             false },
		{ GUI_FULLSCREEN,         false },
		{ GUI_SCREEN_DPI,         false },
		{ GUI_BG_R,               false },
		{ GUI_BG_G,               false },
		{ GUI_BG_B,               false },
		{ GUI_SHOW_INDICATORS,    false },
		{ GUI_UI_SCALING,         false }
	} },
	{ DIALOGS_SECTION, {
		{ DIALOGS_FILE_TYPE,  false },
		{ DIALOGS_FILE_MODE,  false },
		{ DIALOGS_FILE_ORDER, false },
		{ DIALOGS_FILE_ZOOM,  false },
		{ DIALOGS_SAVE_MODE,  false },
		{ DIALOGS_SAVE_ORDER, false },
		{ DIALOGS_SAVE_ZOOM,  false }
	} },
	{ CAPTURE_SECTION, {
		{ CAPTURE_DIR,           false },
		{ CAPTURE_VIDEO_FORMAT,  false },
		{ CAPTURE_VIDEO_QUALITY, false }
	} },
	{ DISPLAY_SECTION, {
		{ DISPLAY_TYPE,              false },
		{ DISPLAY_NORMAL_SCALE,      false },
		{ DISPLAY_NORMAL_ASPECT,     false },
		{ DISPLAY_FILTER,            false },
		{ DISPLAY_NORMAL_SHADER,     false },
		{ DISPLAY_REALISTIC_SHADER,  false },
		{ DISPLAY_AMBIENT,           false },
		{ DISPLAY_BRIGHTNESS,        false },
		{ DISPLAY_CONTRAST,          false },
		{ DISPLAY_SATURATION,        false },
		{ DISPLAY_SAMPLERS_MODE,     true  },
		{ DISPLAY_SHADER_INPUT,      false },
		{ DISPLAY_SHADER_OUTPUT,     false }
	} },
	{ SYSTEM_SECTION, {
		{ SYSTEM_ROMSET, false },
		{ SYSTEM_MODEL,  false }
	} },
	{ CPU_SECTION, {
		{ CPU_MODEL,     false },
		{ CPU_FREQUENCY, false },
		{ CPU_HLT_WAIT,  false }
	} },
	{ MEM_SECTION, {
		{ MEM_RAM_EXP,   false },
		{ MEM_RAM_SPEED, false }
	} },
	{ VGA_SECTION, {
		{ VGA_ROM,        false },
		{ VGA_PS_BIT_BUG, false }
	} },
	{ CMOS_SECTION, {
		{ CMOS_IMAGE_FILE,     false },
		{ CMOS_IMAGE_RTC_INIT, false },
		{ CMOS_IMAGE_SAVE,     false }
	} },
	{ DRIVES_SECTION, {
		{ DRIVES_FDD_A,         false },
		{ DRIVES_FDD_B,         false },
		{ DRIVES_FLOPPY_COMMIT, false },
		{ DRIVES_FDC_TYPE,      false },
		{ DRIVES_FDC_MODE,      false },
		{ DRIVES_FDC_OVR,       true  },
		{ DRIVES_FDD_LAT,       false },
		{ DRIVES_HDC_TYPE,      false },
		{ DRIVES_HDD_COMMIT,    false }
	} },
	{ DISK_A_SECTION, {
		{ DISK_PATH,      false },
		{ DISK_INSERTED,  false },
		{ DISK_READONLY,  false },
		{ DISK_TYPE,      true },
		{ DISK_CYLINDERS, true },
		{ DISK_HEADS,     true }
	} },
	{ DISK_B_SECTION, {
		{ DISK_PATH,      false },
		{ DISK_INSERTED,  false },
		{ DISK_READONLY,  false },
		{ DISK_TYPE,      true },
		{ DISK_CYLINDERS, true },
		{ DISK_HEADS,     true }
	} },
	{ DISK_C_SECTION, {
		{ DISK_TYPE,       false },
		{ DISK_PATH,       false },
		{ DISK_CYLINDERS,  false },
		{ DISK_HEADS,      false },
		{ DISK_SPT,        false },
		{ DISK_SEEK_MAX,   false },
		{ DISK_SEEK_TRK,   false },
		{ DISK_ROT_SPEED,  false },
		{ DISK_INTERLEAVE, false }
	} },
	{ MIXER_SECTION, {
		{ MIXER_PREBUFFER, false },
		{ MIXER_RATE,      false },
		{ MIXER_SAMPLES,   false },
		{ MIXER_VOLUME,    false }
	} },
	{ MIDI_SECTION, {
		{ MIDI_ENABLED, false },
		{ MIDI_DEVICE,  false },
		{ MIDI_DELAY,   false }
	} },
	{ PCSPEAKER_SECTION, {
		{ PCSPEAKER_ENABLED, false },
		{ PCSPEAKER_RATE,    false },
		{ PCSPEAKER_FILTERS, false },
		{ PCSPEAKER_VOLUME,  false }
	} },
	{ PS1AUDIO_SECTION, {
		{ PS1AUDIO_ENABLED, false },
		{ PS1AUDIO_RATE,    false },
		{ PS1AUDIO_FILTERS, false },
		{ PS1AUDIO_VOLUME,  false }
	} },
	{ ADLIB_SECTION, {
		{ ADLIB_ENABLED, false },
		{ ADLIB_RATE,    false },
		{ ADLIB_FILTERS, false },
		{ ADLIB_VOLUME,  false }
	} },
	{ SBLASTER_SECTION, {
		{ SBLASTER_ENABLED,     false },
		{ SBLASTER_IOBASE,      false },
		{ SBLASTER_DMA,         false },
		{ SBLASTER_IRQ,         false },
		{ SBLASTER_DAC_FILTERS, false },
		{ SBLASTER_DAC_VOLUME,  false },
		{ SBLASTER_OPL_RATE,    false },
		{ SBLASTER_OPL_FILTERS, false },
		{ SBLASTER_OPL_VOLUME,  false }
	} },
	{ MPU401_SECTION, {
		{ MPU401_ENABLED, false },
		{ MPU401_IOBASE,  false },
		{ MPU401_IRQ,     false },
		{ MPU401_MODE,    false }
	} },
	{ GAMEPORT_SECTION, {
		{ GAMEPORT_ENABLED, false }
	} },
	{ SOUNDFX_SECTION, {
		{ SOUNDFX_ENABLED,  false },
		{ SOUNDFX_VOLUME,   false },
		{ SOUNDFX_FDD_SPIN, false },
		{ SOUNDFX_FDD_SEEK, false },
		{ SOUNDFX_HDD_SPIN, false },
		{ SOUNDFX_HDD_SEEK, false },
		{ SOUNDFX_SYSTEM,   false },
		{ SOUNDFX_MODEM,    false },
		{ SOUNDFX_MODEM_FILTERS, true  },
		{ SOUNDFX_MODEM_COUNTRY, false }
	} },
	{ SERIAL_SECTION, {
		{ SERIAL_ENABLED,       false },
		{ SERIAL_A_MODE,        false },
		{ SERIAL_A_DEV,         false },
		{ SERIAL_A_TX_DELAY,    false },
		{ SERIAL_A_TCP_NODELAY, false },
		{ SERIAL_A_DUMP,        true  }
	} },
	{ MODEM_SECTION, {
		{ MODEM_BAUD_RATE,    false },
		{ MODEM_LISTEN_ADDR,  false },
		{ MODEM_PHONEBOOK,    false },
		{ MODEM_TELNET_MODE,  false },
		{ MODEM_CONN_TIMEOUT, false },
		{ MODEM_WARM_DELAY,   false },
		{ MODEM_CONNECT_CODE, false },
		{ MODEM_ECHO_ON,      false },
		{ MODEM_HANDSHAKE,    false },
		{ MODEM_DUMP,         true  },
	} },
	{ LPT_SECTION, {
		{ LPT_ENABLED, false },
		{ LPT_PORT,    false },
		{ LPT_FILE,    false }
	} },
	{ PRN_SECTION, {
		{ PRN_CONNECTED,     false },
		{ PRN_MODE,          false },
		{ PRN_COLOR,         false },
		{ PRN_INK,           false },
		{ PRN_PREVIEW_DIV,   false },
		{ PRN_EPSON_CSET,    false },
		{ PRN_IBM_CSET,      false },
		{ PRN_PAPER_SIZE,    false },
		{ PRN_PAPER_TYPE,    false },
		{ PRN_BOF,           false },
		{ PRN_TOP_MARGIN,    false },
		{ PRN_BOTTOM_MARGIN, false }
	} },
};


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

std::string AppConfig::get_value(const std::string &section, const std::string &name, bool _quiet)
{
	std::string valstr;
	try {
		valstr = INIFile::get_value(m_values, section, name);
	} catch(std::exception &) {
		try {
			valstr = INIFile::get_value(ms_def_values[PROGRAM_CONFIG], section, name);
		} catch(std::exception &) {
			try {
				valstr = INIFile::get_value(ms_def_values[MACHINE_CONFIG], section, name);
			} catch(std::exception &) {
				if(!_quiet) {
					PERRF(LOG_PROGRAM, "[%s]:%s is not a valid configuration key!\n", section.c_str(),name.c_str());
				}
				throw;
			}
		}
		if(!_quiet) {
			PWARNF(LOG_V1, LOG_PROGRAM, "[%s]:%s undefined, using default: '%s'\n", section.c_str(),name.c_str(), valstr.c_str());
		}
	}
	return valstr;
}

std::string AppConfig::get_value(const std::string &_section, const std::string &_name)
{
	return get_value(_section, _name, false);
}

void AppConfig::set_user_home(std::string _path)
{
	m_user_home = _path;
}

void AppConfig::set_cfg_home(std::string _path)
{
	m_cfg_home = _path;
	m_user_shaders_path = m_cfg_home + FS_SEP "shaders" FS_SEP;
}

void AppConfig::set_assets_home(std::string _path)
{
	m_assets_home = _path;
	m_assets_shaders_path = m_assets_home + FS_SEP "shaders" FS_SEP;
	m_images_path = m_assets_home + FS_SEP "gui" FS_SEP "images" FS_SEP;
}

std::string AppConfig::find_shader_asset(std::string _relative_file_path)
{
	// return find_file(std::string("shaders") + FS_SEP + _relative_file_path);
	// should be faster:
	try {
		return FileSys::realpath((m_user_shaders_path + _relative_file_path).c_str());
	} catch(std::runtime_error &) {
		return FileSys::realpath((m_assets_shaders_path + _relative_file_path).c_str());
	}
}

std::string AppConfig::find_shader_asset_relative_to(std::string _relative_file_path, std::string _relative_to_abs_file)
{
	std::string abs_directory = FileSys::get_path_dir(_relative_to_abs_file.c_str()) + FS_SEP;
	std::string fullpath = abs_directory + _relative_file_path;
	try {
		fullpath = FileSys::realpath(fullpath.c_str());
	} catch(std::exception &) {
		std::string reference;
		if(abs_directory.find(m_user_shaders_path) == 0) {
			fullpath = m_assets_shaders_path + abs_directory.substr(m_user_shaders_path.size()) + _relative_file_path;
		} else if(_relative_to_abs_file.find(m_assets_shaders_path) == 0) {
			fullpath = m_user_shaders_path + abs_directory.substr(m_assets_shaders_path.size()) + _relative_file_path;
		} else {
			throw;
		}
		fullpath = FileSys::realpath(fullpath.c_str());
	}
	return fullpath;
}

std::string AppConfig::get_file_path(std::string _filename, FileType _type)
{
#ifndef _WIN32
	if(_filename.at(0) == '~') {
		return m_user_home + _filename.substr(1);
	} else if(_filename.at(0) == '/') {
		return _filename;
	}
#else
	if(!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, &_filename[0], -1, 0, 0)) {
		_filename = FileSys::to_utf8(_filename);
	}
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
	assert(false);
	return _filename;
}

std::string AppConfig::get_file(const std::string &_section, const std::string &_name, FileType _type)
{
	std::string filename;
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

std::string AppConfig::try_get_file(const std::string &_section, const std::string &_name, FileType _type)
{
	std::string filename;
	try {
		filename = get_value(_section, _name);
	} catch(std::exception &e) {
		return "";
	}

	if(filename.empty()) {
		return filename;
	}

	return get_file_path(filename, _type);
}

std::string AppConfig::find_file(const std::string &_section, const std::string &_name)
{
	std::string path = get_file(_section, _name, FILE_TYPE_USER);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file(_section, _name, FILE_TYPE_ASSET);
	}
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file(_section, _name, FILE_TYPE_MEDIA);
	}
	if(!FileSys::file_exists(path.c_str())) {
		path = get_value(_section, _name);
	}
	return path;
}

std::string AppConfig::find_file(const std::string &_filename)
{
	std::string path = get_file_path(_filename, FILE_TYPE_USER);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file_path(_filename, FILE_TYPE_ASSET);
	}
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file_path(_filename, FILE_TYPE_MEDIA);
	}
	if(!FileSys::file_exists(path.c_str())) {
		path = _filename;
	}
	return path;
}

std::string AppConfig::find_media(const std::string &_section, const std::string &_name)
{
	std::string path = get_file(_section, _name, FILE_TYPE_MEDIA);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file(_section, _name, FILE_TYPE_USER);
	}
	return path;
}

std::string AppConfig::find_media(const std::string &_filename)
{
	std::string path = get_file_path(_filename, FILE_TYPE_MEDIA);
	if(!FileSys::file_exists(path.c_str())) {
		path = get_file_path(_filename, FILE_TYPE_USER);
	}
	return path;
}

void AppConfig::create_file(const std::string &_filename, bool _savestate)
{
	std::ofstream file = FileSys::make_ofstream(_filename.c_str());
	if(!file.is_open()) {
		PERRF(LOG_FS,"Cannot open '%s' for writing\n",_filename.c_str());
		throw std::exception();
	}
	if(!_savestate) {
		file << ms_help["HEADER"] << std::endl;
	}
	for(auto section : ms_keys_order) {
		auto sname = section.first;
		auto keys = section.second;
		file << "[" << sname << "]" << "\n";
		if(!_savestate) {
			file << ms_help[sname];
		}

		for(auto key : keys) {
			auto kname = key.first;
			auto hidden = key.second;
			if(!_savestate && hidden) {
				continue;
			}
			std::string value;
			try {
				value = get_value(sname, kname, true);
			} catch(...) {
				value = "";
			}
			file << kname << "=" << value << "\n";
		}
		file << std::endl;
	}

	file.close();
}

