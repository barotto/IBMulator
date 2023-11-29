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
#include "mixer.h"
#include <algorithm>
#include <regex>
#include <cctype>
#include <cstdlib>

AppConfig::ConfigHelp AppConfig::ms_help = {
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
";          width: Window width in pixel.\n"
";         height: Window height in pixel (for normal GUI mode it doesn't include the system unit.)\n"
";     fullscreen: Start directly in fullscreen.\n"
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
"; Volumes are expressed as positive real numbers between 0.0 and " MIXER_MAX_VOLUME_STR ", where 1.0 is nominal.\n"
"; Balances are real numbers between -1.0 and 1.0, where -1.0 = left, 0.0 = center, 1.0 = right\n"
"; prebuffer: How many milliseconds of data to prebuffer before audio starts to be emitted. A larger value might help sound stuttering, but will introduce latency.\n"
";            Possible values: any positive integer number between 10 and 1000.\n"
";      rate: Sample rate. Use the value which is more compatible with your sound card. Any emulated device with a rate different than this will be resampled.\n"
";            Possible values: 48000, 44100, 49716.\n"
";   samples: Audio samples buffer size; a larger buffer might help sound stuttering.\n"
";            Possible values: 1024, 2048, 4096, 512, 256.\n"
";   profile: Name of a INI file containing the volumes, effects, and filters of audio channels. You can use the Mixer GUI window to create one.\n"
";    volume: General audio volume for the emulated sound cards.\n"
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
";  volume: Audio volume.\n"
"; balance: Audio balance.\n"
"; filters: DSP filters (see README for more info).\n"
";  reverb: Reverb effect (see README for more info).\n"
";  chorus: Chorus effect (see README for more info).\n"
		},

		{ PS1AUDIO_SECTION,
";     enabled: Install the PS/1 Audio/Joystick Card.\n"
";  dac_volume: Audio volume of the DAC.\n"
"; dac_balance: Audio balance of the DAC.\n"
"; dac_filters: DSP filters for the DAC.\n"
";              Possible values: auto, a preset name, or a list of filter definitions (see the README for more info).\n"
";               auto: use the same value defined for [" PCSPEAKER_SECTION "]:filters\n"
";  dac_reverb: Reverb effect for the DAC.\n"
";              Possible values: auto, or one of the reverb presets (see README for more info)\n"
";               auto: use the same value defined for [" PCSPEAKER_SECTION "]:reverb\n"
";  dac_chorus: Chorus effect for the DAC (see README for more info).\n"
";    psg_rate: Sample rate of the PSG (Programmable Sound Generator).\n"
";              Possible values: 48000, 44100, 32000, 22050, 11025.\n"
";  psg_volume: Audio volume of the PSG.\n"
"; psg_balance: Audio balance of the PSG.\n"
"; psg_filters: DSP filters for the PSG.\n"
";              Possible values: auto, a preset name, or a list of filter definitions (see the README for more info).\n"
";               auto: use the same value defined for [" PCSPEAKER_SECTION "]:filters\n"
";  psg_reverb: Reverb effect for the PSG.\n"
";              Possible values: auto, or one of the reverb presets (see README for more info)\n"
";               auto: use the same value defined for [" PCSPEAKER_SECTION "]:reverb\n"
";  psg_chorus: Chorus effect for the PSG (see README for more info).\n"
		},

		{ ADLIB_SECTION,
"; enabled: Install the AdLib Audio Card (cannot be installed with Sound Blaster).\n"
";    rate: Sample rate. The real AdLib uses a frequency of 49716Hz.\n"
";          Possible values: 48000, 49716, 44100, 32000, 22050, 11025.\n"
";  volume: Audio volume.\n"
"; balance: Audio balance.\n"
"; filters: DSP filters (see README for more info).\n"
";  reverb: Reverb effect (see README for more info).\n"
";  chorus: Chorus effect (see README for more info).\n"
		},

		{ SBLASTER_SECTION,
";        enabled: Install the Sound Blaster audio card.\n"
";          model: The type of the Sound Blaster card.\n"
";                 Possible values: sb2, sbpro, sbpro2\n"
";                     sb2: Sound Blaster 2.0 (DSP 2.01)\n"
";                   sbpro: Sound Blaster Pro (DSP 3.00)\n"
";                  sbpro2: Sound Blaster Pro 2 (DSP 3.02)\n"
";         iobase: The I/O base address, as an hexadecimal number.\n"
";                 Possible values: 0x220, 0x240.\n"
";            irq: The IRQ line number.\n"
";                 Possible values: 3, 5, 7.\n"
";            dma: The DMA channel number.\n"
";                 Possible values: 0, 1, 3.\n"
"; dac_resampling: The resampling method used.\n"
";                 Possible values: auto, sinc, linear, zoh\n"
";                    auto: the method depends on the Sound Blaster model.\n"
";                    sinc: a bandlimited interpolator derived from the sinc function (SNR of 97dB, bandwidth of 90%).\n"
";                  linear: linear converter.\n"
";                     zoh: Zero Order Hold converter (interpolated value is equal to the last value).\n"
";     dac_volume: DAC's MASTER audio volume.\n"
";                 Possible values: auto, or a positive real number.\n"
";                  auto: let the Sound Blater's Mixer adjust the level (SBPro+ only).\n"
"     dac_balance: DAC's audio balance.\n"
";    dac_filters: DAC's audio filters.\n"
";                 Possible values: auto, a preset, or a list of filter definitions (see the README for more info).\n"
";     dac_reverb: Reverb effect for PCM audio (see README for more info).\n"
";     dac_chorus: Chorus effect for PCM audio (see README for more info).\n"
";  dac_crossfeed: Crossfeed for PCM audio (see README for more info).\n"
";       opl_rate: The OPL chip(s) sample rate. The real hardware uses a frequency of 49716Hz.\n"
";                 Possible values: 48000, 49716, 44100, 32000, 22050, 11025.\n"
";     opl_volume: The OPL chip(s) MASTER audio volume.\n"
";                 Possible values: auto, or a positive real number.\n"
";                  auto: let the Sound Blater's Mixer adjust the level (SBPro or later only).\n"
";    opl_balance: The OPL chip(s) audio balance.\n"
";    opl_filters: Audio filters for the OPL chip(s)\n"
";                 Possible values: auto, a preset, or a list of filter definitions (see the README for more info).\n"
";     opl_reverb: Reverb effect for FM audio (see README for more info).\n"
";     opl_chorus: Chorus effect for FM audio (see README for more info).\n"
";  opl_crossfeed: Crossfeed for FM audio (see README for more info).\n"
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
";         enabled: Enable sound effects emulation.\n"
";          volume: General volume of the sound effects.\n"
";          reverb: Reverb effect to apply to all channels (see README for more info).\n"
";        fdd_seek: Volume of FDD seeks.\n"
";        fdd_spin: Volume of FDD spin noise.\n"
";         fdd_gui: Volume of FDD GUI sounds (ie. insert / eject disk).\n"
";     fdd_balance: Balance of FDD noises.\n"
";        hdd_seek: Volume of HDD seeks.\n"
";        hdd_spin: Volume of HDD spin noise.\n"
";     hdd_balance: Balance of HDD noises.\n"
";          system: Volume of system unit's and monitor's noises.\n"
";  system_balance: Balance of system noises.\n"
";           modem: Volume of the serial modem's internal speaker.\n"
";   modem_country: Determines the style of the modem tones.\n"
";                  Possible values: eu, us.\n"
";   modem_balance: Balance of the serial modem's internal speaker.\n"
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
";                 Possible values: 300, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 33600, 56000, 57600, 115200.\n" 
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

AppConfig::ConfigSections AppConfig::ms_sections = {
	{ PROGRAM_SECTION, {
		// key name,           // category     hidden?         default
		{ PROGRAM_MEDIA_DIR,   PROGRAM_CONFIG, PUBLIC_CFGKEY,  ""        },
		{ PROGRAM_LOG_FILE,    PROGRAM_CONFIG, PUBLIC_CFGKEY,  "log.txt" },
		{ PROGRAM_WAIT_METHOD, PROGRAM_CONFIG, HIDDEN_CFGKEY,  "auto"    },
	} },
	{ GUI_SECTION, {
		{ GUI_RENDERER,        PROGRAM_CONFIG, PUBLIC_CFGKEY, "opengl"     },
		{ GUI_FRAMECAP,        PROGRAM_CONFIG, PUBLIC_CFGKEY, "vga"        },
		{ GUI_MODE,            PROGRAM_CONFIG, PUBLIC_CFGKEY, "normal"     },
		{ GUI_COMPACT_TIMEOUT, PROGRAM_CONFIG, PUBLIC_CFGKEY, "1.5"        },
		{ GUI_REALISTIC_ZOOM,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "cycle"      },
		{ GUI_REALISTIC_STYLE, PROGRAM_CONFIG, PUBLIC_CFGKEY, "bright"     },
		{ GUI_KEYMAP,          PROGRAM_CONFIG, PUBLIC_CFGKEY, "keymap.map" },
		{ GUI_MOUSE_TYPE,      MACHINE_CONFIG, PUBLIC_CFGKEY, "ps2"        },
		{ GUI_WIDTH,           PROGRAM_CONFIG, PUBLIC_CFGKEY, "640"        },
		{ GUI_HEIGHT,          PROGRAM_CONFIG, PUBLIC_CFGKEY, "480"        },
		{ GUI_FULLSCREEN,      PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"         },
		{ GUI_BG_R,            PROGRAM_CONFIG, PUBLIC_CFGKEY, "0"          },
		{ GUI_BG_G,            PROGRAM_CONFIG, PUBLIC_CFGKEY, "0"          },
		{ GUI_BG_B,            PROGRAM_CONFIG, PUBLIC_CFGKEY, "0"          },
		{ GUI_SHOW_INDICATORS, PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"         },
		{ GUI_UI_SCALING,      PROGRAM_CONFIG, PUBLIC_CFGKEY, "100%"       },
		{ GUI_MOUSE_GRAB,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "yes"        },
	} },
	{ DIALOGS_SECTION, {
		{ DIALOGS_FILE_TYPE,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "custom" },
		{ DIALOGS_FILE_MODE,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "grid"   },
		{ DIALOGS_FILE_ORDER, PROGRAM_CONFIG, PUBLIC_CFGKEY, "name"   },
		{ DIALOGS_FILE_ZOOM,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "2"      },
		{ DIALOGS_SAVE_MODE,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "grid"   },
		{ DIALOGS_SAVE_ORDER, PROGRAM_CONFIG, PUBLIC_CFGKEY, "date"   },
		{ DIALOGS_SAVE_ZOOM,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "1"      },
	} },
	{ CAPTURE_SECTION, {
		{ CAPTURE_DIR,           PROGRAM_CONFIG, PUBLIC_CFGKEY, ""     },
		{ CAPTURE_VIDEO_FORMAT,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "zmbv" },
		{ CAPTURE_VIDEO_QUALITY, PROGRAM_CONFIG, PUBLIC_CFGKEY, "3"    },
		{ CAPTURE_VIDEO_MODE,    PROGRAM_CONFIG, HIDDEN_CFGKEY, "avi"  },
	} },
	{ DISPLAY_SECTION, {
		{ DISPLAY_TYPE,             MACHINE_CONFIG, PUBLIC_CFGKEY, "color"                       },
		{ DISPLAY_NORMAL_SCALE,     PROGRAM_CONFIG, PUBLIC_CFGKEY, "fill"                        },
		{ DISPLAY_NORMAL_ASPECT,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "4:3"                         },
		{ DISPLAY_FILTER,           PROGRAM_CONFIG, PUBLIC_CFGKEY, "bilinear"                    },
		{ DISPLAY_NORMAL_SHADER,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "normal_mode/stock.slangp"    },
		{ DISPLAY_REALISTIC_SHADER, PROGRAM_CONFIG, PUBLIC_CFGKEY, "realistic_mode/stock.slangp" },
		{ DISPLAY_AMBIENT,          PROGRAM_CONFIG, PUBLIC_CFGKEY, "1.0"                         },
		{ DISPLAY_BRIGHTNESS,       PROGRAM_CONFIG, PUBLIC_CFGKEY, "1.0"                         },
		{ DISPLAY_CONTRAST,         PROGRAM_CONFIG, PUBLIC_CFGKEY, "1.0"                         },
		{ DISPLAY_SATURATION,       PROGRAM_CONFIG, PUBLIC_CFGKEY, "1.0"                         },
		{ DISPLAY_SAMPLERS_MODE,    PROGRAM_CONFIG, HIDDEN_CFGKEY, "auto"                        },
		{ DISPLAY_SHADER_INPUT,     PROGRAM_CONFIG, PUBLIC_CFGKEY, "auto"                        },
		{ DISPLAY_SHADER_OUTPUT,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "native"                      },
	} },
	{ SYSTEM_SECTION, {
		{ SYSTEM_ROMSET,        MACHINE_CONFIG, PUBLIC_CFGKEY, ""     },
		{ SYSTEM_MODEL,         MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ SYSTEM_BIOS_PATCHES,  MACHINE_CONFIG, HIDDEN_CFGKEY, ""     },
	} },
	{ CPU_SECTION, {
		{ CPU_MODEL,     MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ CPU_FREQUENCY, MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ CPU_HLT_WAIT,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "500"  },
	} },
	{ MEM_SECTION, {
		{ MEM_RAM_EXP,   MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ MEM_RAM_SPEED, MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
	} },
	{ VGA_SECTION, {
		{ VGA_ROM,        MACHINE_CONFIG, PUBLIC_CFGKEY, ""   },
		{ VGA_PS_BIT_BUG, MACHINE_CONFIG, PUBLIC_CFGKEY, "no" },
	} },
	{ CMOS_SECTION, {
		{ CMOS_IMAGE_FILE,     MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ CMOS_IMAGE_RTC_INIT, PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"   },
		{ CMOS_IMAGE_SAVE,     PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"  },
	} },
	{ DRIVES_SECTION, {
		{ DRIVES_FDD_A,         MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DRIVES_FDD_B,         MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DRIVES_FLOPPY_COMMIT, PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"  },
		{ DRIVES_FDC_TYPE,      MACHINE_CONFIG, PUBLIC_CFGKEY, "raw"  },
		{ DRIVES_FDC_MODE,      MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DRIVES_FDC_OVR,       MACHINE_CONFIG, HIDDEN_CFGKEY, "250"  },
		{ DRIVES_FDD_LAT,       MACHINE_CONFIG, PUBLIC_CFGKEY, "1.0"  },
		{ DRIVES_HDC_TYPE,      MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DRIVES_HDD_COMMIT,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"  },
	} },
	{ DISK_A_SECTION, {
		{ DISK_PATH,      MACHINE_CONFIG, PUBLIC_CFGKEY, ""     },
		{ DISK_INSERTED,  MACHINE_CONFIG, PUBLIC_CFGKEY, "no"   },
		{ DISK_READONLY,  MACHINE_CONFIG, PUBLIC_CFGKEY, "no"   },
		{ DISK_TYPE,      MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ DISK_CYLINDERS, MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ DISK_HEADS,     MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
	} },
	{ DISK_B_SECTION, {
		{ DISK_PATH,      MACHINE_CONFIG, PUBLIC_CFGKEY, ""     },
		{ DISK_INSERTED,  MACHINE_CONFIG, PUBLIC_CFGKEY, "no"   },
		{ DISK_READONLY,  MACHINE_CONFIG, PUBLIC_CFGKEY, "no"   },
		{ DISK_TYPE,      MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ DISK_CYLINDERS, MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ DISK_HEADS,     MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
	} },
	{ DISK_C_SECTION, {
		{ DISK_TYPE,        MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_PATH,        MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_CYLINDERS,   MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_HEADS,       MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_SPT,         MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_SEEK_MAX,    MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_SEEK_TRK,    MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_ROT_SPEED,   MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_INTERLEAVE,  MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ DISK_SPINUP_TIME, MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
	} },
	{ MIXER_SECTION, {
		{ MIXER_PREBUFFER, PROGRAM_CONFIG, PUBLIC_CFGKEY, "50"                },
		{ MIXER_RATE,      PROGRAM_CONFIG, PUBLIC_CFGKEY, "48000"             },
		{ MIXER_SAMPLES,   PROGRAM_CONFIG, PUBLIC_CFGKEY, "1024"              },
		{ MIXER_PROFILE,   PROGRAM_CONFIG, PUBLIC_CFGKEY, "mixer-profile.ini" },
		{ MIXER_VOLUME,    MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"               },
	} },
	{ MIDI_SECTION, {
		{ MIDI_ENABLED, PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"  },
		{ MIDI_DEVICE,  PROGRAM_CONFIG, PUBLIC_CFGKEY, ""     },
		{ MIDI_DEVTYPE, PROGRAM_CONFIG, HIDDEN_CFGKEY, ""     },
		{ MIDI_DELAY,   PROGRAM_CONFIG, PUBLIC_CFGKEY, "auto" },
	} },
	{ PCSPEAKER_SECTION, {
		{ PCSPEAKER_ENABLED, MACHINE_CONFIG, PUBLIC_CFGKEY, "yes"   },
		{ PCSPEAKER_RATE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "48000" },
		{ PCSPEAKER_VOLUME,  MIXER_CONFIG,   PUBLIC_CFGKEY, "0.5"   },
		{ PCSPEAKER_BALANCE, MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"   },
		{ PCSPEAKER_FILTERS, MIXER_CONFIG,   PUBLIC_CFGKEY, "pc-speaker" },
		{ PCSPEAKER_REVERB,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ PCSPEAKER_CHORUS,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
	} },
	{ PS1AUDIO_SECTION, {
		{ PS1AUDIO_ENABLED,     MACHINE_CONFIG, PUBLIC_CFGKEY, "yes"   },
		{ PS1AUDIO_DAC_VOLUME,  MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"   },
		{ PS1AUDIO_DAC_BALANCE, MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"   },
		{ PS1AUDIO_DAC_FILTERS, MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ PS1AUDIO_DAC_REVERB,  MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ PS1AUDIO_DAC_CHORUS,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ PS1AUDIO_PSG_RATE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "48000" },
		{ PS1AUDIO_PSG_VOLUME,  MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"   },
		{ PS1AUDIO_PSG_BALANCE, MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"   },
		{ PS1AUDIO_PSG_FILTERS, MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ PS1AUDIO_PSG_REVERB,  MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ PS1AUDIO_PSG_CHORUS,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
	} },
	{ ADLIB_SECTION, {
		{ ADLIB_ENABLED, MACHINE_CONFIG, PUBLIC_CFGKEY, "no"    },
		{ ADLIB_RATE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "48000" },
		{ ADLIB_FILTERS, MIXER_CONFIG,   PUBLIC_CFGKEY, ""      },
		{ ADLIB_VOLUME,  MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"   },
		{ ADLIB_BALANCE, MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"   },
		{ ADLIB_REVERB,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ ADLIB_CHORUS,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
	} },
	{ SBLASTER_SECTION, {
		{ SBLASTER_ENABLED,        MACHINE_CONFIG, PUBLIC_CFGKEY, "no"    },
		{ SBLASTER_MODEL,          MACHINE_CONFIG, PUBLIC_CFGKEY, "sb2"   },
		{ SBLASTER_IOBASE,         MACHINE_CONFIG, PUBLIC_CFGKEY, "0x220" },
		{ SBLASTER_DMA,            MACHINE_CONFIG, PUBLIC_CFGKEY, "1"     },
		{ SBLASTER_IRQ,            MACHINE_CONFIG, PUBLIC_CFGKEY, "5"     },
		{ SBLASTER_DAC_RESAMPLING, PROGRAM_CONFIG, PUBLIC_CFGKEY, "auto"  },
		{ SBLASTER_DAC_FILTERS,    MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ SBLASTER_DAC_VOLUME,     MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ SBLASTER_DAC_BALANCE,    MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"   },
		{ SBLASTER_DAC_REVERB,     MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ SBLASTER_DAC_CHORUS,     MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ SBLASTER_DAC_CROSSFEED,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ SBLASTER_OPL_RATE,       PROGRAM_CONFIG, PUBLIC_CFGKEY, "48000" },
		{ SBLASTER_OPL_FILTERS,    MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ SBLASTER_OPL_VOLUME,     MIXER_CONFIG,   PUBLIC_CFGKEY, "auto"  },
		{ SBLASTER_OPL_BALANCE,    MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"   },
		{ SBLASTER_OPL_REVERB,     MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ SBLASTER_OPL_CHORUS,     MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
		{ SBLASTER_OPL_CROSSFEED,  MIXER_CONFIG,   PUBLIC_CFGKEY, "no"    },
	} },
	{ MPU401_SECTION, {
		{ MPU401_ENABLED, MACHINE_CONFIG, PUBLIC_CFGKEY, "no"          },
		{ MPU401_IOBASE,  MACHINE_CONFIG, PUBLIC_CFGKEY, "0x330"       },
		{ MPU401_IRQ,     MACHINE_CONFIG, PUBLIC_CFGKEY, "3"           },
		{ MPU401_MODE,    MACHINE_CONFIG, PUBLIC_CFGKEY, "intelligent" },
	} },
	{ GAMEPORT_SECTION, {
		{ GAMEPORT_ENABLED, MACHINE_CONFIG, PUBLIC_CFGKEY, "yes" },
	} },
	{ SOUNDFX_SECTION, {
		{ SOUNDFX_ENABLED,        PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"  },
		{ SOUNDFX_VOLUME,         MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"  },
		{ SOUNDFX_REVERB,         MIXER_CONFIG,   PUBLIC_CFGKEY, "no"   },
		{ SOUNDFX_FDD_SPIN,       MIXER_CONFIG,   PUBLIC_CFGKEY, "0.4"  },
		{ SOUNDFX_FDD_SEEK,       MIXER_CONFIG,   PUBLIC_CFGKEY, "0.4"  },
		{ SOUNDFX_FDD_GUI,        MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"  },
		{ SOUNDFX_FDD_BALANCE,    MIXER_CONFIG,   PUBLIC_CFGKEY, "-0.3" },
		{ SOUNDFX_HDD_SPIN,       MIXER_CONFIG,   PUBLIC_CFGKEY, "0.4"  },
		{ SOUNDFX_HDD_SEEK,       MIXER_CONFIG,   PUBLIC_CFGKEY, "0.4"  },
		{ SOUNDFX_HDD_BALANCE,    MIXER_CONFIG,   PUBLIC_CFGKEY, "0.3"  },
		{ SOUNDFX_SYSTEM,         MIXER_CONFIG,   PUBLIC_CFGKEY, "1.0"  },
		{ SOUNDFX_SYSTEM_BALANCE, MIXER_CONFIG,   PUBLIC_CFGKEY, "0.0"  },
		{ SOUNDFX_MODEM,          MIXER_CONFIG,   PUBLIC_CFGKEY, "1.2"  },
		{ SOUNDFX_MODEM_FILTERS,  MIXER_CONFIG,   HIDDEN_CFGKEY, "auto" },
		{ SOUNDFX_MODEM_BALANCE,  MIXER_CONFIG,   PUBLIC_CFGKEY, "-0.5" },
		{ SOUNDFX_MODEM_COUNTRY,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "eu"   },
	} },
	{ SERIAL_SECTION, {
		{ SERIAL_ENABLED,       MACHINE_CONFIG, PUBLIC_CFGKEY, "yes"  },
		{ SERIAL_A_MODE,        MACHINE_CONFIG, PUBLIC_CFGKEY, "auto" },
		{ SERIAL_A_DEV,         MACHINE_CONFIG, PUBLIC_CFGKEY, ""     },
		{ SERIAL_A_TX_DELAY,    MACHINE_CONFIG, PUBLIC_CFGKEY, "20"   },
		{ SERIAL_A_TCP_NODELAY, MACHINE_CONFIG, PUBLIC_CFGKEY, "yes"  },
		{ SERIAL_A_DUMP,        PROGRAM_CONFIG, HIDDEN_CFGKEY, ""     },
		{ SERIAL_B_MODE,        MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ SERIAL_B_DEV,         MACHINE_CONFIG, HIDDEN_CFGKEY, ""     },
		{ SERIAL_B_TX_DELAY,    MACHINE_CONFIG, HIDDEN_CFGKEY, "20"   },
		{ SERIAL_B_TCP_NODELAY, MACHINE_CONFIG, HIDDEN_CFGKEY, "yes"  },
		{ SERIAL_B_DUMP,        PROGRAM_CONFIG, HIDDEN_CFGKEY, ""     },
		{ SERIAL_C_MODE,        MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ SERIAL_C_DEV,         MACHINE_CONFIG, HIDDEN_CFGKEY, ""     },
		{ SERIAL_C_TX_DELAY,    MACHINE_CONFIG, HIDDEN_CFGKEY, "20"   },
		{ SERIAL_C_TCP_NODELAY, MACHINE_CONFIG, HIDDEN_CFGKEY, "yes"  },
		{ SERIAL_C_DUMP,        PROGRAM_CONFIG, HIDDEN_CFGKEY, ""     },
		{ SERIAL_D_MODE,        MACHINE_CONFIG, HIDDEN_CFGKEY, "auto" },
		{ SERIAL_D_DEV,         MACHINE_CONFIG, HIDDEN_CFGKEY, ""     },
		{ SERIAL_D_TX_DELAY,    MACHINE_CONFIG, HIDDEN_CFGKEY, "20"   },
		{ SERIAL_D_TCP_NODELAY, MACHINE_CONFIG, HIDDEN_CFGKEY, "yes"  },
		{ SERIAL_D_DUMP,        PROGRAM_CONFIG, HIDDEN_CFGKEY, ""     },
	} },
	{ MODEM_SECTION, {
		{ MODEM_BAUD_RATE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "2400"      },
		{ MODEM_LISTEN_ADDR,  PROGRAM_CONFIG, PUBLIC_CFGKEY, ""          },
		{ MODEM_PHONEBOOK,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "phone.txt" },
		{ MODEM_TELNET_MODE,  PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"        },
		{ MODEM_CONN_TIMEOUT, PROGRAM_CONFIG, PUBLIC_CFGKEY, "10"        },
		{ MODEM_WARM_DELAY,   PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"        },
		{ MODEM_CONNECT_CODE, PROGRAM_CONFIG, PUBLIC_CFGKEY, "auto"      },
		{ MODEM_ECHO_ON,      PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"       },
		{ MODEM_HANDSHAKE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"        },
		{ MODEM_DUMP,         PROGRAM_CONFIG, HIDDEN_CFGKEY, ""          },
	} },
	{ LPT_SECTION, {
		{ LPT_ENABLED, MACHINE_CONFIG, PUBLIC_CFGKEY, "yes"  },
		{ LPT_PORT,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "LPT1" },
		{ LPT_FILE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, ""     },
	} },
	{ PRN_SECTION, {
		{ PRN_CONNECTED,     PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"     },
		{ PRN_MODE,          PROGRAM_CONFIG, PUBLIC_CFGKEY, "epson"  },
		{ PRN_COLOR,         PROGRAM_CONFIG, PUBLIC_CFGKEY, "no"     },
		{ PRN_INK,           PROGRAM_CONFIG, PUBLIC_CFGKEY, "medium" },
		{ PRN_PREVIEW_DIV,   PROGRAM_CONFIG, PUBLIC_CFGKEY, "low"    },
		{ PRN_EPSON_CSET,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "basic"  },
		{ PRN_IBM_CSET,      PROGRAM_CONFIG, PUBLIC_CFGKEY, "intl1"  },
		{ PRN_PAPER_SIZE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "letter" },
		{ PRN_PAPER_TYPE,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "forms"  },
		{ PRN_BOF,           PROGRAM_CONFIG, PUBLIC_CFGKEY, "0"      },
		{ PRN_TOP_MARGIN,    PROGRAM_CONFIG, PUBLIC_CFGKEY, "auto"   },
		{ PRN_BOTTOM_MARGIN, PROGRAM_CONFIG, PUBLIC_CFGKEY, "0.1875" },
		{ PRN_SHOW_HEAD,     PROGRAM_CONFIG, PUBLIC_CFGKEY, "yes"    },
	} },
	
	{ LOG_SECTION, {
		{ LOG_OVERRIDE_VERBOSITY, PROGRAM_CONFIG, HIDDEN_CFGKEY, "no" },
		{ LOG_PROGRAM_VERBOSITY,  PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_FS_VERBOSITY,       PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_GFX_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_INPUT_VERBOSITY,    PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_GUI_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_MACHINE_VERBOSITY,  PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_MIXER_VERBOSITY,    PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_MEM_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_CPU_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_MMU_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_PIT_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_PIC_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_DMA_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_KEYB_VERBOSITY,     PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_VGA_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_CMOS_VERBOSITY,     PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_FDC_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_HDD_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_AUDIO_VERBOSITY,    PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_GAMEPORT_VERBOSITY, PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_LPT_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_PRN_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_COM_VERBOSITY,      PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
		{ LOG_MIDI_VERBOSITY,     PROGRAM_CONFIG, HIDDEN_CFGKEY, "0"  },
	} },
};

ini_file_t AppConfig::ms_default_values;
AppConfig::ConfigKeys AppConfig::ms_keys;

AppConfig::AppConfig()
: INIFile()
{
	if(ms_keys.empty()) {
		for(auto &section : ms_sections) {
			for(auto &key : section.keys) {
				ms_keys[section.name][key.name] = key;
				ms_default_values[section.name][key.name] = key.default_value;
			}
		}
	}
}

void AppConfig::reset()
{
	m_values.clear();
}

void AppConfig::merge(const AppConfig &_other, ConfigType _type)
{
	for(auto &other_section : _other.m_values) {
		auto section = other_section.first;
		for(auto &other_entry : other_section.second) {
			auto key = other_entry.first;
			if(ms_keys.find(section) == ms_keys.end()) {
				PDEBUGF(LOG_V0, LOG_PROGRAM, "ini section [%s] not found!\n", section.c_str());
				continue;
			}
			if(ms_keys[section].find(key) == ms_keys[section].end()) {
				PDEBUGF(LOG_V0, LOG_PROGRAM, "ini key [%s]:%s not found!\n", section.c_str(), key.c_str());
				continue;
			}
			ConfigType ktype = ms_keys[section][key].type;
			if(_type != ANY_CONFIG && ktype != _type) {
				PDEBUGF(LOG_V2, LOG_PROGRAM, "ignoring ini key [%s]:%s\n", section.c_str(), key.c_str());
				continue;
			}
			// it will insert a new section in the current values if not present
			m_values[section][key] = other_entry.second;
		}
	}
}

void AppConfig::copy(const AppConfig &_config)
{
	(*this) = _config;
}

std::string AppConfig::get_value_default(const std::string &section, const std::string &name) noexcept
{
	try {
		return INIFile::get_value(ms_default_values, section, name);
	} catch(std::exception &) {}

	assert(false);
	return "";
}

std::string AppConfig::get_value(const std::string &section, const std::string &name, bool _quiet)
{
	std::string valstr;
	try {
		valstr = INIFile::get_value(m_values, section, name);
	} catch(std::exception &) {
		try {
			valstr = INIFile::get_value(ms_default_values, section, name);
		} catch(std::exception &) {
			if(!_quiet) {
				PERRF(LOG_PROGRAM, "[%s]:%s is not a valid configuration key!\n", section.c_str(), name.c_str());
			}
			throw;
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

int AppConfig::get_int_or_default(const std::string &section, const std::string &name) noexcept
{
	// will try to parse the user's value string, will return the default if the value is not specified
	// or isn't valid.
	int value, def = get_int_default(section, name);
	try {
		// try_int() will throw when:
		// 1. <section,name> isn't a valid config pair (bug)
		// 2. the value string is not a valid integer
		// if value string is not specified, the default will be used
		value = try_int(section, name);
	} catch(std::exception &) {
		// assume the user's value string is not a valid integer
		PERRF(LOG_PROGRAM, "The value specified in '[%s]:%s' is not a valid integer, using default: %d\n",
				section.c_str(), name.c_str(), def);
		value = def;
	}

	return value;
}

double AppConfig::get_real_or_default(const std::string &section, const std::string &name) noexcept
{
	// see comments for get_int_or_default()
	double value, def = get_real_default(section, name);
	try {
		value = try_real(section, name);
	} catch(std::exception &) {
		PERRF(LOG_PROGRAM, "The value specified in '[%s]:%s' is not a valid real number, using default: %.2f\n",
				section.c_str(), name.c_str(), def);
		value = def;
	}

	return value;
}

int AppConfig::get_int_or_default(const std::string &section, const std::string &name, int min, int max) noexcept
{
	int value = get_int_or_default(section, name);
	if(value < min || value > max) {
		int def = get_int_default(section, name);
		// the default must be within range
		assert(value != def);
		PERRF(LOG_PROGRAM, "The value specified in '[%s]:%s' is out of range, using default: %d\n",
				section.c_str(), name.c_str(), def);
		return def;
	}
	return value;
}

double AppConfig::get_real_or_default(const std::string &section, const std::string &name, double min, double max) noexcept
{
	// see comments for get_int_or_default()
	double value = get_real_or_default(section, name);

	if(value < min || value > max) {
		double def = get_real_default(section, name);
		assert(value != def);
		PERRF(LOG_PROGRAM, "The value specified in '[%s]:%s' is out of range, using default: %.2f\n",
				section.c_str(), name.c_str(), def);
		return def;
	}
	return value;
}

bool AppConfig::get_bool_or_default(const std::string &section, const std::string &name) noexcept
{
	// see comments for get_int_or_default()
	try {
		return try_bool(section, name);
	} catch(std::exception &) {
		bool def = get_bool_default(section, name);
		PERRF(LOG_PROGRAM, "The value specified in '[%s]:%s' is not a valid boolean, using default: %s\n",
				section.c_str(), name.c_str(), def ? "yes" : "no");
		return def;
	}
}

std::string AppConfig::get_string_or_default(const std::string &_section, const std::string &_name) noexcept
{
	try {
		if(is_key_present(_section, _name)) {
			return get_value(_section, _name);
		}
	} catch(std::exception &) {}
	return get_value_default(_section, _name);
}

int AppConfig::get_int_default(const std::string &section, const std::string &name) noexcept
{
	try {
		return parse_int(get_value_default(section, name));
	} catch(std::exception &) {
		// the default value is not a valid integer (bug)
		assert(false);
		return 0;
	}
}

double AppConfig::get_real_default(const std::string &section, const std::string &name) noexcept
{
	try {
		return parse_real(get_value_default(section, name));
	} catch(std::exception &) {
		assert(false);
		return 0.0;
	}
}

bool AppConfig::get_bool_default(const std::string &section, const std::string &name) noexcept
{
	try {
		return parse_bool(get_value_default(section, name));
	} catch(std::exception &) {
		assert(false);
		return false;
	}
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
		PINFOF(LOG_V2, LOG_OGL, "Resolving '%s'\n", fullpath.c_str());
		fullpath = FileSys::realpath(fullpath.c_str());
	} catch(std::exception &) {
		PWARNF(LOG_V0, LOG_OGL, "Cannot resolve '%s'\n", fullpath.c_str());
		std::string reference;
		if(abs_directory.find(m_user_shaders_path) == 0) {
			fullpath = m_assets_shaders_path + abs_directory.substr(m_user_shaders_path.size()) + _relative_file_path;
		} else if(_relative_to_abs_file.find(m_assets_shaders_path) == 0) {
			fullpath = m_user_shaders_path + abs_directory.substr(m_assets_shaders_path.size()) + _relative_file_path;
		} else {
			throw;
		}
		PWARNF(LOG_V0, LOG_OGL, "Trying to resolve '%s'\n", fullpath.c_str());
		try {
			fullpath = FileSys::realpath(fullpath.c_str());
		} catch(std::exception &) {
			PERRF(LOG_OGL, "Cannot resolve '%s'\n", fullpath.c_str());
			throw std::runtime_error(str_format("cannot find %s", _relative_file_path.c_str()));
		}
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

void AppConfig::create_file(const std::string &_filename, ConfigType _type, bool _comments, bool _savestate)
{
	std::ofstream file = FileSys::make_ofstream(_filename.c_str());
	if(!file.is_open()) {
		PERRF(LOG_FS, "Cannot open '%s' for writing\n", _filename.c_str());
		throw std::runtime_error("Cannot open file for writing");
	}
	if(_comments) {
		file << ms_help["HEADER"] << "\n";
	}
	for(auto &section : ms_sections) {
		std::string keys_str;
		for(auto &key : section.keys) {
			if((_type != ANY_CONFIG && key.type != _type) || (!_savestate && key.visibility != PUBLIC_CFGKEY)) {
				continue;
			}
			std::string value;
			try {
				value = get_value(section.name, key.name, true);
			} catch(...) {
				value = "";
			}
			keys_str += key.name + "=" + value + "\n";
		}
		if(!keys_str.empty()) {
			file << "[" << section.name << "]" << "\n";
			if(_comments) {
				file << ms_help[section.name];
			}
			file << keys_str << "\n";
			keys_str = "";
		}
	}

	file.close();
}

