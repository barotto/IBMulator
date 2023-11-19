/*
 * Copyright (C) 2020-2023  Marco Bortolin
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
/*
 * ADPCM decoders Copyright (C) 2002-2015  The DOSBox Team
 */

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "sblaster.h"
#include "pic.h"
#include "dma.h"
#include "audio/convert.h"
#include "wincompat.h"

IODEVICE_PORTS(SBlaster) = {};

static const IODevice::IOPorts adlib_ports = {
	{ 0x388, 0x389, PORT_8BIT|PORT_RW } // AdLib compatibility
};
// Sound Blaster 1.x and 2.0 ports
static const IODevice::IOPorts sb_ports = {
	{ 0x0, 0x3, PORT_8BIT|PORT__W }, // C/MS
	{ 0x6, 0x7, PORT_8BIT|PORT__W }, // DSP Reset
	{ 0x8, 0x9, PORT_8BIT|PORT_RW }, // OPL2
	{ 0xa, 0xb, PORT_8BIT|PORT_R_ }, // DSP Read Data
	{ 0xc, 0xd, PORT_8BIT|PORT_RW }, // DSP Write Command/Data and Buffer status
	{ 0xe, 0xf, PORT_8BIT|PORT_R_ }  // DSP Read Buffer status
};
// Sound Blaster Pro and Pro 2 ports
static const IODevice::IOPorts sbpro_ports = {
	{ 0x0, 0x3, PORT_8BIT|PORT_RW }, // Dual-OPL2 (left/right) or OPL3 banks 0/1
	{ 0x4, 0x4, PORT_8BIT|PORT__W }, // Mixer Register address
	{ 0x5, 0x5, PORT_8BIT|PORT_RW }, // Mixer Data
	{ 0x6, 0x7, PORT_8BIT|PORT__W }, // DSP Reset
	{ 0x8, 0x9, PORT_8BIT|PORT_RW }, // OPL2 (center) or OPL3 bank 0
	{ 0xa, 0xb, PORT_8BIT|PORT_R_ }, // DSP Read Data
	{ 0xc, 0xd, PORT_8BIT|PORT_RW }, // DSP Write Command/Data and Buffer status
	{ 0xe, 0xf, PORT_8BIT|PORT_R_ }  // DSP Read Buffer status
};

#define SB_DSP_DATARDY 0x80
#define SB_DSP_RSTRDY  0xAA
#define SB_DSP_NOCMD   0x00
static const char *SB16_COPYRIGHT = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";

// On DSP version < 4.00 the busy cycle time depends on the I/O rate and stops when
// no I/O is performed.
// On DSP version >= 4.00 the busy cycle is always active and the DSP busy bit toggles
// by itself at an unknown clock rate.
// See https://github.com/joncampbell123/dosbox-x/wiki/Hardware%3ASound-Blaster%3ADSP-busy-cycle
// These timings are completely made up, no docs. 
#define SB_DSP_BUSYTIME     10_us
#define SB_DEFAULT_CMD_TIME 1_us
#define SB_DAC_TIMEOUT      1_s

enum DSPVMask {
	DSP1 = 0x1, DSP2 = 0x2, DSP3 = 0x4, DSP4 = 0x8, DSPALL = 0xf
};

#define DSPVHI ((m_dsp_ver >> 8) & 0xf)
#define DSPVLO (m_dsp_ver & 0xf)
#define ISDSPV(__x__) (DSPVHI == (__x__))
#define DSP_VMASK ( 1 << (DSPVHI - 1) )

using namespace std::placeholders;

#define DSP_CMD(_hex_, _dsp_, _len_, _time_, _fn_, _string_) \
	{ _hex_, { (_dsp_), _len_, _time_, _fn_, _string_ } }

constexpr uint8_t REF = 0x80;
constexpr bool AUTO = true;
constexpr bool SINGLE = false;
constexpr bool HI = true;
constexpr bool LO = false;
constexpr bool MIDIPOLL = true;
constexpr bool MIDIINT = false;

// TODO: the command jump table has no dummy code for unimplemented commands. Every
// function 00-FF has some code to run. However, the jump addresses are repeated.
// For example on SB2.0 (DSP 2.01) the Set Time Constant function (0x40) is valid for commands
// 0x40 - 0x47.
// See: https://www.vogons.org/viewtopic.php?f=62&t=61098&start=287
const std::multimap<int, SBlaster::DSPCmd> SBlaster::ms_dsp_commands = {
	DSP_CMD( 0x03, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "ASP Status" ),
	DSP_CMD( 0x04, DSP2|DSP3,      0, 0,      &SBlaster::dsp_cmd_status,                          "DSP Status" ),
	DSP_CMD( 0x04, DSP4,           1, 0,      &SBlaster::dsp_cmd_unimpl,                          "ASP set mode" ),
	DSP_CMD( 0x05, DSP4,           2, 0,      &SBlaster::dsp_cmd_unimpl,                          "ASP set codec parameter" ),
	DSP_CMD( 0x08, DSP4,           1, 0,      &SBlaster::dsp_cmd_unimpl,                          "ASP get version" ),
	DSP_CMD( 0x0e, DSP4,           2, 0,      &SBlaster::dsp_cmd_unimpl,                          "ASP set register" ),
	DSP_CMD( 0x0f, DSP4,           1, 0,      &SBlaster::dsp_cmd_unimpl,                          "ASP get register" ),
	DSP_CMD( 0x10, DSPALL,         1, 0,      &SBlaster::dsp_cmd_direct_dac_8,                    "Direct DAC, 8-bit" ),
	DSP_CMD( 0x14, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 8,     SINGLE, LO), "Single Cycle DMA DAC, 8-Bit" ),
	DSP_CMD( 0x15, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 8,     SINGLE, LO), "Single Cycle DMA DAC, 8-Bit" ),
	DSP_CMD( 0x16, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 2,     SINGLE, LO), "Single Cycle DMA DAC, 2-bit ADPCM" ),
	DSP_CMD( 0x17, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 2|REF, SINGLE, LO), "Single Cycle DMA DAC, 2-bit ADPCM Ref" ),
	DSP_CMD( 0x1c, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 8,     AUTO,   LO), "Auto-Init DMA DAC, 8-bit" ),
	DSP_CMD( 0x1f, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 2|REF, AUTO,   LO), "Auto-Init DMA DAC, 2-bit ADPCM Ref" ),
	DSP_CMD( 0x20, DSPALL,         0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Direct ADC, 8-bit" ),
	DSP_CMD( 0x24, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_adc, _1, 8,     SINGLE, LO), "Single Cycle DMA ADC, 8-Bit" ),
	DSP_CMD( 0x28, DSP1|DSP2|DSP3, 0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Direct ADC, 8-bit (Burst)" ),
	DSP_CMD( 0x2c, DSP2|DSP3|DSP4, 0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Auto-Init DMA ADC, 8-bit" ),
	DSP_CMD( 0x30, DSPALL,         0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Poll mode MIDI input" ),
	DSP_CMD( 0x31, DSPALL,         0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Int mode MIDI input" ),
	DSP_CMD( 0x32, DSPALL,         0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Poll mode MIDI input w/ time stamp" ),
	DSP_CMD( 0x33, DSPALL,         0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Int mode MIDI input w/ time stamp" ),
	DSP_CMD( 0x34, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_midi_uart, _1, MIDIPOLL, false), "UART poll mode MIDI I/O" ),
	DSP_CMD( 0x35, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_midi_uart, _1, MIDIINT, false),  "UART int mode MIDI I/O" ),
	DSP_CMD( 0x36, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_midi_uart, _1, MIDIPOLL, true),  "UART poll mode MIDI I/O w/ time stamp" ),
	DSP_CMD( 0x37, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_midi_uart, _1, MIDIINT, true),   "UART int mode MIDI I/O w/ time stamp" ),
	DSP_CMD( 0x38, DSPALL,         1, 0,      &SBlaster::dsp_cmd_midi_out,                        "MIDI output" ),
	DSP_CMD( 0x40, DSPALL,         1, 0,      &SBlaster::dsp_cmd_set_time_const,                  "Set Time Constant" ),
	DSP_CMD( 0x41, DSP4,           2, 0,      &SBlaster::dsp_cmd_unimpl,                          "Set Output Samplerate" ),
	DSP_CMD( 0x42, DSP4,           2, 0,      &SBlaster::dsp_cmd_unimpl,                          "Set Input Samplerate" ),
	DSP_CMD( 0x45, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Continue Auto-Init DMA, 8-bit" ),
	DSP_CMD( 0x47, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Continue Auto-Init DMA, 16-bit" ),
	DSP_CMD( 0x48, DSP2|DSP3|DSP4, 2, 0,      &SBlaster::dsp_cmd_set_dma_block,                   "Set DMA Block Size" ),
	DSP_CMD( 0x74, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 4,     SINGLE, LO), "Single Cycle DMA DAC, 4-bit ADPCM" ),
	DSP_CMD( 0x75, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 4|REF, SINGLE, LO), "Single Cycle DMA DAC, 4-bit ADPCM Ref" ),
	DSP_CMD( 0x76, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 3,     SINGLE, LO), "Single Cycle DMA DAC, 2.6-bit ADPCM" ),
	DSP_CMD( 0x77, DSPALL,         2, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 3|REF, SINGLE, LO), "Single Cycle DMA DAC, 2.6-bit ADPCM Ref" ),
	DSP_CMD( 0x7d, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 4|REF, AUTO,   LO), "Auto-Init DMA DAC, 4-bit ADPCM Ref" ),
	DSP_CMD( 0x7f, DSP2|DSP3|DSP4, 0, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 3|REF, AUTO,   LO), "Auto-Init DMA DAC, 2.6-bit ADPCM Ref" ),
	DSP_CMD( 0x80, DSPALL,         2, 0,      &SBlaster::dsp_cmd_pause_dac,                       "Pause DAC" ),
	DSP_CMD( 0x90, DSP2|DSP3,      0, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 8,     AUTO,   HI), "Auto-Init DMA DAC, 8-bit (High Speed)" ),
	DSP_CMD( 0x91, DSP2|DSP3,      0, 0, bind(&SBlaster::dsp_cmd_dma_dac, _1, 8,     SINGLE, HI), "Single Cycle DMA DAC, 8-bit (High speed)" ),
	DSP_CMD( 0x98, DSP2|DSP3,      0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Auto-Init DMA ADC, 8-bit (High Speed)" ),
	DSP_CMD( 0x99, DSP2|DSP3,      0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Single Cycle DMA ADC, 8-bit (High Speed)" ),
	DSP_CMD( 0xa0, DSP3,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Set input mode to mono" ),
	DSP_CMD( 0xa8, DSP3,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Set input mode to stereo" ),
	DSP_CMD( 0xb0, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb1, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb2, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb3, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb4, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb5, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb6, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb7, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb8, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xb9, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xba, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xbb, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xbc, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xbd, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xbe, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xbf, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 16-bit DMA" ),
	DSP_CMD( 0xc0, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc1, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc2, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc3, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc4, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc5, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc6, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc7, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc8, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xc9, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xca, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xcb, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xcc, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xcd, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xce, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xcf, DSP4,           3, 0,      &SBlaster::dsp_cmd_unimpl,                          "Generic 8-bit DMA" ),
	DSP_CMD( 0xd0, DSPALL,         0, 0,      &SBlaster::dsp_cmd_pause_dma_8,                     "Pause DMA, 8-bit" ),
	DSP_CMD( 0xd1, DSPALL,         0, 0,      &SBlaster::dsp_cmd_speaker_on,                      "Enable speaker" ),
	DSP_CMD( 0xd3, DSPALL,         0, 0,      &SBlaster::dsp_cmd_speaker_off,                     "Disable speaker" ),
	DSP_CMD( 0xd4, DSPALL,         0, 0,      &SBlaster::dsp_cmd_continue_dma_8,                  "Continue DMA, 8-bit" ),
	DSP_CMD( 0xd5, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Pause DMA, 16-bit" ),
	DSP_CMD( 0xd6, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Continue DMA, 16-bit" ),
	DSP_CMD( 0xd8, DSP2|DSP3|DSP4, 0, 0,      &SBlaster::dsp_cmd_speaker_status,                  "Speaker status" ),
	DSP_CMD( 0xd9, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Exit Auto-Init, 16-bit" ),
	DSP_CMD( 0xda, DSP2|DSP3|DSP4, 0, 0,      &SBlaster::dsp_cmd_exit_ai_dma_8,                   "Exit Auto-Init, 8-bit" ),
	DSP_CMD( 0xe0, DSP2|DSP3|DSP4, 1, 0,      &SBlaster::dsp_cmd_identify,                        "DSP Identification" ),
	DSP_CMD( 0xe1, DSPALL,         0, 0,      &SBlaster::dsp_cmd_get_version,                     "Get DSP Version" ),
	DSP_CMD( 0xe2, DSPALL,         1, 0,      &SBlaster::dsp_cmd_identify_dma,                    "DMA identification" ),
	DSP_CMD( 0xe3, DSP4,           0, 0,      &SBlaster::dsp_cmd_get_copyright,                   "DSP Copyright" ),
	DSP_CMD( 0xe4, DSP2|DSP3|DSP4, 1, 0,      &SBlaster::dsp_cmd_write_test_reg,                  "Write Test Register" ),
	DSP_CMD( 0xe8, DSP2|DSP3|DSP4, 0, 0,      &SBlaster::dsp_cmd_read_test_reg,                   "Read Test Register" ),
	DSP_CMD( 0xf0, DSPALL,         0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Sine Generator" ),
	DSP_CMD( 0xf1, DSP1|DSP2|DSP3, 0, 0,      &SBlaster::dsp_cmd_aux_status,                      "Auxiliary status" ),
	DSP_CMD( 0xf2, DSPALL,         0,20,      &SBlaster::dsp_cmd_trigger_irq_8,                   "Trigger IRQ, 8-bit" ),
	DSP_CMD( 0xf3, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Trigger IRQ, 16-bit" ),
	DSP_CMD( 0xf8, DSP1|DSP2|DSP3, 0, 0,      &SBlaster::dsp_cmd_f8_unknown,                      "Unknown" ),
	DSP_CMD( 0xf9, DSP4,           1, 0,      &SBlaster::dsp_cmd_unimpl,                          "Set register value" ),
	DSP_CMD( 0xfa, DSP4,           1, 0,      &SBlaster::dsp_cmd_unimpl,                          "Get register value" ),
	DSP_CMD( 0xfb, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Status" ),
	DSP_CMD( 0xfc, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Auxiliary status" ),
	DSP_CMD( 0xfd, DSP4,           0, 0,      &SBlaster::dsp_cmd_unimpl,                          "Command Status" )
};

// TODO not sure about these values
#define SB_TC_45454 0xea // 234, 45454 Hz
#define SB_TC_44100 0xe9 // 233, 43478 Hz
#define SB_TC_23000 0xd4 // 212, 22727 Hz
#define SB_TC_22050 0xd3 // 211, 22222 Hz
#define SB_TC_15000 0xbd // 189, 14925 Hz
#define SB_TC_13000 0xb3 // 179, 12987 Hz
#define SB_TC_12000 0xac // 172, 11904 Hz
#define SB_TC_11000 0xa5 // 165, 10989 Hz
#define SB_TC_4000  0x06 // 006, 4000 Hz

constexpr uint16_t time_const_to_freq(uint8_t _tc) {
	return (256000000UL / (65536UL - (uint32_t(_tc) << 8)));
}

#define SB_MIXER_0VOL false // true will set volumes to 0.0 instead to -46dB when mixer's value is 0.


SBlaster::SBlaster(Devices *_dev)
: IODevice(_dev), Synth()
{
	memset(&m_s, 0, sizeof(m_s));
	m_s.dac.device = this;
}

void SBlaster::install_dsp(int _version, std::string _filters)
{
	// HARDWARE
	m_dsp_ver = _version;

	using namespace std::placeholders;
	m_dsp_timer = g_machine.register_timer(
		std::bind(&SBlaster::dsp_timer, this, _1),
		"SBlaster DSP"
	);
	m_dma_timer = g_machine.register_timer(
		std::bind(&SBlaster::dma_timer, this, _1),
		"SBlaster DMA"
	);
	m_dac_timer = g_machine.register_timer(
		std::bind(&SBlaster::dac_timer, this, _1),
		"SBlaster DAC"
	);

	register_dma(g_program.config().get_int(SBLASTER_SECTION, SBLASTER_DMA));
	register_irq(g_program.config().get_int(SBLASTER_SECTION, SBLASTER_IRQ));

	// AUDIO CHANNEL
	m_dac_channel = g_mixer.register_channel(
		std::bind(&SBlaster::dac_create_samples, this, _1, _2, _3),
		std::string(short_name()) + " DAC", MixerChannel::AUDIOCARD, MixerChannel::AudioType::DAC);
	m_dac_channel->set_disable_timeout(5_s);
	m_dac_filters = _filters;

	unsigned features =
		MixerChannel::HasVolume |
		MixerChannel::HasBalance |
		MixerChannel::HasReverb |
		MixerChannel::HasChorus | 
		MixerChannel::HasFilter | 
		MixerChannel::HasResamplingType |
		MixerChannel::HasAutoResamplingType;
	if(_version >= 0x300) {
		features |=
			MixerChannel::HasStereoSource |
			MixerChannel::HasAutoVolume |
			MixerChannel::HasAutoFilter |
			MixerChannel::HasCrossfeed;
	}
	m_dac_channel->set_features(features);

	m_dac_channel->add_autoval_cb(MixerChannel::ConfigParameter::Volume, std::bind(&SBlaster::update_volumes, this));
	m_dac_channel->add_autoval_cb(MixerChannel::ConfigParameter::Filter,
			std::bind(&SBlaster::auto_filter_cb, this, m_dac_channel.get(), m_dac_filters));
	m_dac_channel->add_autoval_cb(MixerChannel::ConfigParameter::Resampling, std::bind(&SBlaster::auto_resampling_cb, this));

	m_dac_channel->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SBLASTER_SECTION, SBLASTER_DAC_VOLUME }},
		{ MixerChannel::ConfigParameter::Reverb, { SBLASTER_SECTION, SBLASTER_DAC_REVERB }},
		{ MixerChannel::ConfigParameter::Chorus, { SBLASTER_SECTION, SBLASTER_DAC_CHORUS }},
		{ MixerChannel::ConfigParameter::Filter, { SBLASTER_SECTION, SBLASTER_DAC_FILTERS }},
		{ MixerChannel::ConfigParameter::Crossfeed, { SBLASTER_SECTION, SBLASTER_DAC_CROSSFEED }},
		{ MixerChannel::ConfigParameter::Resampling, { SBLASTER_SECTION, SBLASTER_DAC_RESAMPLING }},
	});
}

void SBlaster::install_opl(OPL::ChipType _chip_type, int _count, bool _has_mixer, std::string _filters)
{
	int channels = 1;

	if(_count == 1) {

		m_OPL[0].install(_chip_type, OPL::ChipNames[_chip_type], true);

		Synth::set_chip(0, &m_OPL[0]);

		if(_chip_type == OPL::OPL2) {
			Synth::install(std::string(short_name()) + " FM", 5_s,
				[this](Event &_event) {
					m_OPL[0].write(0, _event.reg);
					m_OPL[0].write(1, _event.value);
					capture_command(0x5A, _event);
				},
				[this](AudioBuffer &_buffer, int _sample_offset, int _frames) {
					m_OPL[0].generate(&_buffer.operator[]<int16_t>(_sample_offset), _frames, 1);
				},
				[this](bool _start, VGMFile& _vgm) {
					if(_start) {
						_vgm.set_chip(VGMFile::YM3812);
						_vgm.set_clock(3579545);
						_vgm.set_tag_system("IBM PC");
						_vgm.set_tag_notes(full_name());
					}
				}
			);
		} else {
			channels = 2;
			Synth::install(std::string(short_name()) + " FM", 5_s,
				[this](Event &_event) {
					m_OPL[0].write(_event.reg_port, _event.reg);
					m_OPL[0].write(_event.value_port, _event.value);
					capture_command(0x5E + ((_event.reg_port >> 1) & 1), _event);
				},
				[this](AudioBuffer &_buffer, int _sample_offset, int _frames) {
					m_OPL[0].generate(&_buffer.operator[]<int16_t>(_sample_offset), _frames, 2);
				},
				[this](bool _start, VGMFile& _vgm) {
					if(_start) {
						_vgm.set_chip(VGMFile::YMF262);
						_vgm.set_clock(14318180);
						_vgm.set_tag_system("IBM PC");
						_vgm.set_tag_notes(full_name());
					}
				}
			);
		}

	} else if(_count == 2) {

		assert(_chip_type == OPL::OPL2);
		channels = 2;

		m_OPL[0].install(OPL::OPL2, std::string(OPL::ChipNames[OPL::OPL2]) + " L", true);
		m_OPL[1].install(OPL::OPL2, std::string(OPL::ChipNames[OPL::OPL2]) + " R", true);

		Synth::set_chip(0, &m_OPL[0]);
		Synth::set_chip(1, &m_OPL[1]);

		Synth::install(std::string(short_name()) + " FM", 5_s,
			[this](Event &_event) {
				m_OPL[_event.chip].write(0, _event.reg);
				m_OPL[_event.chip].write(1, _event.value);
				capture_command(0x5A + 0x50*_event.chip, _event);
			},
			[this](AudioBuffer &_buffer, int _sample_offset, int _frames) {
				m_OPL[0].generate(&_buffer.operator[]<int16_t>(_sample_offset  ), _frames, 2); // left
				m_OPL[1].generate(&_buffer.operator[]<int16_t>(_sample_offset+1), _frames, 2); // right
			},
			[this](bool _start, VGMFile& _vgm) {
				if(_start) {
					_vgm.set_chip(VGMFile::YM3812);
					// enable dual chip (bit 30) and left-right separation (bit 31)
					_vgm.set_clock(3579545 | 0xC0000000);
					_vgm.set_tag_system("IBM PC");
					_vgm.set_tag_notes(full_name());
				}
			}
		);

	} else {

		assert(false);
		throw std::exception();

	}

	m_opl_filters = _filters;

	unsigned features =
		MixerChannel::HasVolume |
		MixerChannel::HasBalance |
		MixerChannel::HasReverb |
		MixerChannel::HasChorus |
		MixerChannel::HasFilter | MixerChannel::HasAutoFilter;
	if(channels == 2) {
		features |= 
			MixerChannel::HasStereoSource |
			MixerChannel::HasCrossfeed;
	}
	if(_has_mixer) {
		features |= MixerChannel::HasAutoVolume;
	}
	Synth::channel()->set_features(features);

	Synth::channel()->add_autoval_cb(MixerChannel::ConfigParameter::Volume, std::bind(&SBlaster::update_volumes, this));
	Synth::channel()->add_autoval_cb(MixerChannel::ConfigParameter::Filter,
			std::bind(&SBlaster::auto_filter_cb, this, Synth::channel(), m_opl_filters));

	Synth::channel()->register_config_map({
		{ MixerChannel::ConfigParameter::Volume, { SBLASTER_SECTION, SBLASTER_OPL_VOLUME }},
		{ MixerChannel::ConfigParameter::Reverb, { SBLASTER_SECTION, SBLASTER_OPL_REVERB }},
		{ MixerChannel::ConfigParameter::Chorus, { SBLASTER_SECTION, SBLASTER_OPL_CHORUS }},
		{ MixerChannel::ConfigParameter::Filter, { SBLASTER_SECTION, SBLASTER_OPL_FILTERS }},
		{ MixerChannel::ConfigParameter::Crossfeed, { SBLASTER_SECTION, SBLASTER_OPL_CROSSFEED }},
	});
}

void SBlaster::install()
{
	install_ports(sb_ports);
	install_dsp(0x105, "");
	install_opl(OPL::OPL2, 1, false, "LowPass,order=1,fc=12000"); // single YM3812 (OPL2)

	PINFOF(LOG_V0, LOG_AUDIO, "Installed %s (%s)\n", full_name(), blaster_env().c_str());
}

void SBlaster2::install()
{
	install_ports(sb_ports);
	install_dsp(0x201, "");
	install_opl(OPL::OPL2, 1, false, "LowPass,order=1,fc=12000"); // single YM3812 (OPL2)

	PINFOF(LOG_V0, LOG_AUDIO, "Installed %s (%s)\n", full_name(), blaster_env().c_str());
}

void SBlasterPro1::install()
{
	install_ports(sbpro_ports);
	install_dsp(0x300, "LowPass,order=2,fc=3200");
	install_opl(OPL::OPL2, 2, true, "LowPass,order=1,fc=8000"); // dual YM3812 (OPL2)

	PINFOF(LOG_V0, LOG_AUDIO, "Installed %s (%s)\n", full_name(), blaster_env().c_str());
}

void SBlasterPro2::install()
{
	install_ports(sbpro_ports);
	install_dsp(0x302, "LowPass,order=2,fc=3200");
	install_opl(OPL::OPL3, 1, true, "LowPass,order=1,fc=8000"); // single YMF262 (OPL3)

	PINFOF(LOG_V0, LOG_AUDIO, "Installed %s (%s)\n", full_name(), blaster_env().c_str());
}

std::string SBlaster::blaster_env()
{
	return str_format("A%03X I%u D%u T%u", m_iobase, m_irq, m_dma, type());
}

void SBlaster::install_ports(const IODevice::IOPorts &_ports)
{
	ms_ioports.clear();
	ms_ioports.insert(ms_ioports.end(), adlib_ports.begin(), adlib_ports.end());
	ms_ioports.insert(ms_ioports.end(), _ports.begin(), _ports.end());

	register_ports(0, g_program.config().get_int(SBLASTER_SECTION, SBLASTER_IOBASE));
}

void SBlaster::register_ports(unsigned _old_base, unsigned _new_base)
{
	rebase_ports(ms_ioports.begin()+1, ms_ioports.end(), _old_base, _new_base);
	IODevice::install();
	m_iobase = _new_base;
}

void SBlaster::register_dma(unsigned _channel)
{
	m_devices->dma()->register_8bit_channel(_channel,
			std::bind(&SBlaster::dma_read_8, this, _1, _2, _3),
			std::bind(&SBlaster::dma_write_8, this, _1, _2, _3),
			nullptr,
			name());
	m_dma = _channel;
}

void SBlaster::register_irq(unsigned _line)
{
	g_machine.register_irq(_line, name());
	m_irq = _line;
}

void SBlaster::config_changed()
{
	unsigned opl_rate = clamp(g_program.config().get_int(SBLASTER_SECTION, SBLASTER_OPL_RATE),
			MIXER_MIN_RATE, MIXER_MAX_RATE);

	unsigned channels = 1;
	if(m_OPL[0].type() == OPL::OPL3 || Synth::get_chip(1)) {
		channels = 2;
	}
	Synth::config_changed({AUDIO_FORMAT_S16, channels, double(opl_rate)});

	bool updated = false;
	unsigned new_base = g_program.config().get_int(SBLASTER_SECTION, SBLASTER_IOBASE);
	if(new_base != m_iobase) {
		IODevice::remove();
		register_ports(m_iobase, new_base);
		updated = true;
	}
	unsigned new_dma = g_program.config().get_int(SBLASTER_SECTION, SBLASTER_DMA);
	if(new_dma != m_dma) {
		m_devices->dma()->unregister_channel(m_dma);
		register_dma(new_dma);
		updated = true;
	}
	unsigned new_irq = g_program.config().get_int(SBLASTER_SECTION, SBLASTER_IRQ);
	if(new_irq != m_irq) {
		g_machine.unregister_irq(m_irq, name());
		register_irq(new_irq);
		updated = true;
	}
	if(updated) {
		PINFOF(LOG_V0, LOG_AUDIO, "Installed %s (%s)\n", full_name(), blaster_env().c_str());
	}
}

void SBlaster::remove()
{
	IODevice::remove();
	Synth::remove();

	g_mixer.unregister_channel(m_dac_channel);
	
	m_devices->dma()->unregister_channel(m_dma);
	g_machine.unregister_irq(m_irq, name());
	
	g_machine.unregister_timer(m_dsp_timer);
	g_machine.unregister_timer(m_dma_timer);
	g_machine.unregister_timer(m_dac_timer);
}

void SBlaster::reset(unsigned)
{
	Synth::reset();
	m_s.dsp.high_speed = false;
	dsp_reset();
	m_s.dsp.out.lastval = SB_DSP_RSTRDY;
	mixer_reset();
}

void SBlaster::dsp_reset()
{
	lower_interrupt();
	
	if(m_s.dsp.high_speed || m_s.dsp.mode == DSP::Mode::MIDI_UART) {
		// The DSP reset command behaves differently while the DSP is in high-speed mode or MIDI. It
		// terminates high-speed/MIDI mode and restores all DSP parameters to the states prior to
		// entering the high-speed/MIDI mode.
		PDEBUGF(LOG_V1, LOG_AUDIO, "%s DSP: reset (%s)\n", short_name(), 
				m_s.dsp.high_speed?"High Speed":"MIDI UART");
		std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
		dsp_change_mode(DSP::Mode::NONE);
		dac_set_state(DAC::State::STOPPED);
		dma_stop();
		dsp_update_frequency();
		m_s.dsp.state = DSP::State::NORMAL;
		return;
	}
	
	PDEBUGF(LOG_V1, LOG_AUDIO, "%s DSP: reset\n", short_name());
	
	// reset the DSP
	m_s.dsp.in.flush();
	m_s.dsp.out.flush();
	m_s.dsp.cmd = SB_DSP_NOCMD;
	m_s.dsp.cmd_len = 0;
	m_s.dsp.cmd_in_pos = 0;
	m_s.dsp.state = DSP::State::NORMAL;
	m_s.dsp.mode = DSP::Mode::NONE;
	m_s.dsp.time_const = 45;
	m_s.dsp.decoder = DSP::Decoder::PCM;
	g_machine.deactivate_timer(m_dsp_timer);
	
	// reset the DMA engine
	dma_stop();
	m_s.dma.count = 0;
	m_s.dma.left = 0;
	m_s.dma.autoinit = 0;
	m_s.dma.drq = false;
	m_s.dma.irq = false;
	m_s.dma.mode = DMA::Mode::NONE;
	m_s.dma.identify.vadd = 0xAA;
	m_s.dma.identify.vxor = 0x96;
	
	// reset the DAC
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	m_s.dac.spec.channels = 1;
	dsp_update_frequency();
	dac_set_state(DAC::State::STOPPED);
	m_s.dac.change_format(AUDIO_FORMAT_U8);
	m_s.dac.speaker = false;
	m_s.dac.irq_count = 0;

	update_volumes();
}

void SBlaster::power_off()
{
	Synth::power_off();
	m_dac_channel->enable(false);
}

uint16_t SBlaster::read(uint16_t _address, unsigned)
{
	uint16_t value = ~0;
	uint16_t address = _address;
	
	if(_address >= 0x388 && _address <= 0x389) {
		address -= 0x380;
	} else {
		address -= m_iobase;
	}
	switch(address) {
		case 0x0: case 0x1: // CMS or OPL chip/port 0
		case 0x2: case 0x3: // CMS or OPL chip/port 1
		case 0x8: case 0x9: // OPL chip/port 0
			value = read_fm(address);
			break;
		case 0x5: // Mixer (Pro and Pro 2 only)
			value = read_mixer(address);
			break;
		case 0xa: case 0xb: // DSP Read data
		case 0xc: case 0xd: // DSP Write status
		case 0xe: case 0xf: // DSP Read status
			value = read_dsp(address);
			break;
		default:
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: unhandled read from port %d 0x%04X!\n",
					short_name(), address, _address);
			break;
	}

	return value;
}

uint16_t SBlaster::read_fm(uint16_t _address)
{
	uint8_t value = m_OPL[0].read(_address & 3);
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s FM: read  c0:p%d         -> %02Xh\n",
			short_name(), _address, value);
	return value;
}

uint16_t SBlasterPro1::read_fm(uint16_t _address)
{
	uint8_t chip = (_address>>1) & 1;
	uint8_t value = m_OPL[chip].read(_address & 3);
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s FM: read  c%d:p%d         -> %02Xh\n",
			short_name(), chip, _address, value);
	return value;
}

uint16_t SBlaster::read_mixer(uint16_t _address)
{
	UNUSED(_address);
	
	PDEBUGF(LOG_V0, LOG_AUDIO, "%s: Mixer not installed!\n", short_name());
	return ~0;
}

uint16_t SBlasterPro::read_mixer(uint16_t)
{
	// CT1345
	assert(m_s.mixer.reg_idx < sizeof(m_s.mixer.reg));

	uint8_t value = ~0;
	switch(m_s.mixer.reg_idx) {
		case 0x02: // Master CT1335 compat
			value = (m_s.mixer.reg[0x22] & 0x0e) | 1;
			break;
		case 0x06: // FM CT1335 compat
			value = (m_s.mixer.reg[0x26] & 0x0e) | 1;
			break;
		case 0x08: // CD CT1335 compat
			value = (m_s.mixer.reg[0x28] & 0x0e) | 1;
			break;
		default:
			value = m_s.mixer.reg[m_s.mixer.reg_idx];
			break;
	}
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s Mixer: read  0x5 -> 0x%02X\n", short_name(), value);
	return value;
}

uint16_t SBlaster::read_dsp(uint16_t _address)
{
	uint8_t value = 0x7f;
	switch(_address) {
		case 0xa: case 0xb: // Read Data
			value = m_s.dsp.out.read();
			break;
		case 0xc: case 0xd: // Write-Buffer Status
			// If bit-7 is 0, the DSP buffer is empty and is ready to
			// receive commands or data.
			switch(m_s.dsp.state) {
				case DSP::State::NORMAL: {
					unsigned busy = g_machine.get_virt_time_ns() % m_s.dac.period_ns;
					if(m_s.dsp.mode == DSP::Mode::DMA && (m_s.dsp.high_speed || busy<SB_DSP_BUSYTIME)) {
						// TODO in SB16 the busy cycle is always active.
						// with 16bit reads, 8 bits will have the busy bit set, 
						// and 8 will have the busy bit clear.

						// DSP is busy processing
						value |= 0x80;
					} else {
						value |= (m_s.dsp.in.used >= DSP::BUFSIZE) << 7;
					}
					break;
				}
				case DSP::State::EXEC_CMD:
				case DSP::State::RESET_START:
				case DSP::State::RESET:
					// Respond with "busy", but if the program writes don't discard.
					value |= 0x80;
					break;
			}
			break;
		case 0xe: case 0xf: // Read-Buffer Status
			// interrupt is acknowledged by reading the DSP Read-Buffer Status port once.
			lower_interrupt();
			if(m_s.dsp.out.used) {
				value |= 0x80;
			}
			// Real hardware probably returns something else for bits 0-6.
			// Eg. SB Pro 2 returns 0x2A for empty and 0xAA for full.
			break;
		default:
			assert(false);
			break;
	}
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: read  0x%x -> 0x%02X\n", short_name(), _address, value);
	
	return value;
}

void SBlaster::write(uint16_t _address, uint16_t _value, unsigned)
{
	uint16_t addr = _address;

	if(_address >= 0x388 && _address <= 0x389) {
		addr -= 0x380;
	} else {
		addr -= m_iobase;
	}

	switch(addr) {
		case 0x0: case 0x1: // CMS or OPL chip/port 0
		case 0x2: case 0x3: // CMS or OPL chip/port 1
		case 0x8: case 0x9: // OPL chip/port 0
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s FM: write 0x%x <- 0x%02x\n", short_name(), _address, _value);
			write_fm(addr, _value);
			break;
		case 0x4: case 0x5:
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s Mixer: write 0x%x <- 0x%02x\n", short_name(), _address, _value);
			write_mixer(addr, _value);
			break;
		case 0x6: case 0x7: // DSP reset
		case 0xc: case 0xd: // DSP Write Command/Data
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: write 0x%x <- 0x%02x\n", short_name(), _address, _value);
			write_dsp(addr, _value);
			break;
		default:
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: unhandled write to port 0x%04X!\n", short_name(), _address);
			break;
	}
}

void SBlaster::write_fm(uint16_t _address, uint16_t _value)
{
	if(_address <= 3) {
		// TODO
		// write_cms(_address, _value);
		return;
	}
	// 8 & 9 OPL ports
	write_fm(0, _address-8, _value);
}

void SBlaster2::write_fm(uint16_t _address, uint16_t _value)
{
	SBlaster::write_fm(0, _address & 0x3, _value);
}

void SBlasterPro1::write_fm(uint16_t _address, uint16_t _value)
{
	switch(_address) {
		case 0: case 1: // left OPL
			SBlaster::write_fm(0, _address, _value);
			break;
		case 2: case 3: // right OPL
			SBlaster::write_fm(1, _address-2, _value);
			break;
		case 8: case 9: // center, both OPL chips
			SBlaster::write_fm(0, _address-8, _value);
			SBlaster::write_fm(1, _address-8, _value);
			break;
		default:
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: invalid FM port write!\n", short_name());
			break;
	}
}

void SBlasterPro2::write_fm(uint16_t _address, uint16_t _value)
{
	switch(_address) {
		case 0: case 1: // OPL2 / OPL3 bank 0
		case 2: case 3: // OPL3 bank 1
		case 8: case 9: // OPL2 / OPL3 bank 0
			SBlaster::write_fm(0, _address, _value);
			break;
		default:
			PDEBUGF(LOG_V0, LOG_AUDIO, "%s: invalid FM port write!\n", short_name());
			return;
	}
}

void SBlaster::write_dsp(uint16_t _address, uint16_t _value)
{
	switch(_address) {
		case 0x6: case 0x7: // DSP reset
		{
			bool reset = _value & 1;
			if(reset && m_s.dsp.state != DSP::State::RESET_START) {
				m_s.dsp.state = DSP::State::RESET_START;
				// stop any pending operation
				g_machine.deactivate_timer(m_dsp_timer);
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: Reset start\n", short_name());
			} else if(!reset && m_s.dsp.state == DSP::State::RESET_START) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: RESET\n", short_name());
				// do the reset procedure now and flush the data buffers.
				dsp_reset();
				m_s.dsp.state = DSP::State::RESET;
				// complete the reset successfully with 0xAA result after 20 us.
				g_machine.activate_timer(m_dsp_timer, 50_us, false);
			} else {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: Invalid reset procedure?\n", short_name());
			}
			break;
		}
		case 0xc: case 0xd: // DSP Write Command/Data
		{
			if(m_s.dsp.high_speed) {
				// TODO is this correct?
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: write in high speed, ignored\n", short_name());
				break;
			}
			m_s.dsp.in.write(_value);
			if(m_s.dsp.state == DSP::State::NORMAL) {
				dsp_read_in_buffer();
			}
			break;
		}
		default:
			assert(false);
			break;
	}
}

const SBlaster::DSPCmd * SBlaster::dsp_decode_cmd(uint8_t _cmd)
{
	const DSPCmd *cmd = nullptr;
	auto range = ms_dsp_commands.equal_range(_cmd);
	for(auto i = range.first; i != range.second; i++) {
		if(i->second.dsp_vmask & DSP_VMASK) {
			cmd = &i->second;
			break;
		}
	}
	return cmd;
}

void SBlaster::dsp_start_cmd(const DSPCmd *_cmd)
{
	assert(m_s.dsp.cmd != SB_DSP_NOCMD);
	assert(_cmd);

	m_s.dsp.state = DSP::State::EXEC_CMD;
	uint64_t cmdtime = US_TO_NS(_cmd->time_us);
	if(!cmdtime) {
		cmdtime = SB_DEFAULT_CMD_TIME;
	}
	g_machine.activate_timer(m_dsp_timer, cmdtime, false);
}

void SBlaster::write_mixer(uint16_t _address, uint16_t _value)
{
	UNUSED(_address);
	UNUSED(_value);
	
	PDEBUGF(LOG_V0, LOG_AUDIO, "%s: Mixer not installed!\n", short_name());
}

void SBlasterPro::write_mixer(uint16_t _address, uint16_t _value)
{
	// CT1345

	if(_address == 0x04) {
		m_s.mixer.reg[0x01] = m_s.mixer.reg[m_s.mixer.reg_idx];
		m_s.mixer.reg_idx = _value & 0xff;
	} else if(_address == 0x05) {
		assert(m_s.mixer.reg_idx < sizeof(m_s.mixer.reg));
		switch(m_s.mixer.reg_idx) {
			case 0x00:
				mixer_reset();
				return;
			case 0x02: // CT1335 Master
				m_s.mixer.reg[0x22] = _value | _value << 4;
				break;
			case 0x06: // CT1335 FM
				m_s.mixer.reg[0x26] = _value | _value << 4;
				break;
			case 0x08: // CT1335 CD
				m_s.mixer.reg[0x28] = _value | _value << 4;
				break;
			case 0x04:
				m_s.mixer.reg[0x04] = _value;
				debug_print_volumes(0x04, "DAC");
				break;
			case 0x22:
				m_s.mixer.reg[0x22] = _value;
				debug_print_volumes(0x22, "MASTER");
				break;
			case 0x26:
				m_s.mixer.reg[0x26] = _value;
				debug_print_volumes(0x26, "FM");
				break;
			case 0x0E:
				if((m_s.mixer.reg[0x0E] & 0x02) != (_value & 0x02)) {
					// stereo mode
					PDEBUGF(LOG_V1, LOG_AUDIO, "%s Mixer: stereo mode %s.\n", short_name(),
							(_value & 0x02) ? "ENABLED" : "DISABLED");
				}
				break;
			default:
				break;
		}
		m_s.mixer.reg[m_s.mixer.reg_idx] = _value;
		update_volumes();
	} else {
		PDEBUGF(LOG_V0, LOG_AUDIO, "%s Mixer: invalid register %x\n", _address);
	}
}

void SBlaster::write_fm(uint8_t _chip, uint16_t _address, uint16_t _value)
{
	uint8_t port = _address & 3;
	switch(port) {
		case 0:
		case 2:
		{
			m_s.opl.reg[_chip] = _value;
			m_s.opl.reg_port[_chip] = port;
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s FM: write c%d:p%d index   <- %02Xh\n",
					short_name(), _chip, _address, _value);
			break;
		}
		case 1:
		case 3:
		{
			uint8_t reg = m_s.opl.reg[_chip];
			if(m_s.opl.reg_port[_chip] == 0 && (reg == 2 || reg == 3 || reg == 4)) {
				// timers must be written to immediately.
				m_OPL[_chip].write_timers(reg, _value);
			}
			// the Synth will generate audio in another thread.
			Synth::add_event({
				g_machine.get_virt_time_ns(),
				_chip,
				m_s.opl.reg_port[_chip], reg,
				port, uint8_t(_value)
			});
			Synth::enable_channel();
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s FM: write c%d:p%d reg %02Xh <- %02Xh\n",
					short_name(), _chip, _address, reg, _value
			);
			break;
		}
		default:
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s FM: invalid port %02Xh\n", short_name(), _address);
			break;
	}
}

void SBlaster::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "%s: saving state\n", full_name());
	_state.write(&m_s, {sizeof(m_s), name()});
	Synth::save_state(_state);
}

void SBlaster::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_AUDIO, "%s: restoring state\n", full_name());
	_state.read(&m_s, {sizeof(m_s), name()});
	m_s.dac.device = this;
	Synth::restore_state(_state);

	update_volumes();

	if(m_s.dac.state != DAC::State::STOPPED || m_s.dac.used != 0) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s  DSP mode:%d, DAC state:%d,%u\n", short_name(),
				m_s.dsp.mode, m_s.dac.state, m_s.dac.used);
		m_s.dac.newdata = true;
		m_dac_channel->enable(true);
	}
}

void SBlasterPro::mixer_reset()
{
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s Mixer: RESET\n", short_name());
	
	memset(&m_s.mixer, 0, sizeof(m_s.mixer));

	// default values according to "Sound Blaster Programming Information v0.90 by André Baresel - Craig Jackson"
	// default level = 4
	// bits 0 and 4 are always 1
	m_s.mixer.reg[0x04] = 0x99; // DAC
	m_s.mixer.reg[0x22] = 0x99; // Master
	m_s.mixer.reg[0x26] = 0x99; // FM

	debug_print_volumes(0x04, "DAC");
	debug_print_volumes(0x22, "MASTER");
	debug_print_volumes(0x26, "FM");

	update_volumes();
}

std::pair<int,int> SBlasterPro::get_mixer_levels(uint8_t _reg)
{
	int left = (m_s.mixer.reg[_reg] >> 5) & 0x7;
	int right = (m_s.mixer.reg[_reg] >> 1) & 0x7;

	return { left, right };
}

std::pair<float,float> SBlasterPro::get_mixer_volume_db(uint8_t _reg)
{
	// These values are derived from DOSBox's code.
	// Since they don't seem to be documented anywhere I'm assuming they are the results of direct measurements.
	static const float db_lookup[8] = {
		-46.f, -27.f, -21.f, -16.f, -11.f, -7.f, -3.f, 0.f
	};
	auto [left, right] = get_mixer_levels(_reg);

	return { db_lookup[left], db_lookup[right] };
}

std::pair<float,float> SBlasterPro::get_mixer_volume(uint8_t _reg)
{
	auto [left_db, right_db] = get_mixer_volume_db(_reg);
	
	float left = MixerChannel::db_to_factor(left_db);
	float right = MixerChannel::db_to_factor(right_db);

#if SB_MIXER_0VOL
	if(left_db <= -46.f) {
		left = 0.f;
	}
	if(right_db <= -46.f) {
		right = 0.f;
	}
#endif

	return { left, right };
}

void SBlaster::auto_filter_cb(MixerChannel *_ch, std::string _filter)
{
	// called by the Mixer thread
	if(_ch->is_filter_auto()) {
		_ch->set_filter(_filter);
	}
	update_volumes();
}

void SBlaster::auto_resampling_cb()
{
	// called by the Mixer thread
	if(m_dac_channel->is_resampling_auto()) {
		m_dac_channel->set_resampling_type(MixerChannel::LINEAR);
	}
}

void SBlaster::update_volumes()
{
	if(m_s.dac.speaker) {
		m_dac_channel->set_force_muted(false);
	} else {
		m_dac_channel->set_force_muted(true);
	}
}

void SBlasterPro::update_volumes()
{
	// called by the Machine and Mixer threads
	std::lock_guard<std::mutex> lock(m_volume_mutex);

	// MASTER
	auto [master_l, master_r] = get_mixer_volume(0x22);

	// DAC
	if(m_dac_channel->is_volume_auto()) {
		// dac levels are used for stereo effects in some games (UW2)
		// auto (mixer) volume affects the master volume only
		if(m_dac_channel->volume_master_left() != master_l || m_dac_channel->volume_master_right() != master_r) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s Mixer: DAC master vol L:%.3f - R:%.3f\n", short_name(),
					master_l, master_r);
		}
		m_dac_channel->set_volume_master(master_l, master_r);
	}
	if(m_s.dac.speaker) {
		// SPEAKER on
		m_dac_channel->set_force_muted(false);
		auto [dac_l, dac_r] = get_mixer_volume(0x04);
		if(m_dac_channel->volume_sub_left() != dac_l || m_dac_channel->volume_sub_right() != dac_r) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s Mixer: DAC vol L:%.3f - R:%.3f\n", short_name(),
					dac_l, dac_r);
		}
		m_dac_channel->set_volume_sub(dac_l, dac_r);
	} else {
		// SPEAKER off
		m_dac_channel->set_force_muted(true);
	}
	if(m_dac_channel->is_volume_auto()) {
		// output low-pass filter
		bool enabled = (m_s.mixer.reg[0x0E] & 0x20) == 0;
		if(m_dac_channel->is_filter_enabled() != enabled) {
			m_dac_channel->enable_filter(enabled);
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s Mixer: DAC output filter %s.\n", short_name(),
					enabled ? "ENABLED" : "DISABLED");
		}
	}

	// FM
	auto [fm_l, fm_r] = get_mixer_volume(0x26);
	if(Synth::channel()->is_volume_auto()) {
		if(Synth::channel()->volume_master_left() != master_l || Synth::channel()->volume_master_right() != master_r) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s Mixer: FM master vol L:%.3f - R:%.3f\n", short_name(),
					master_l, master_r);
		}
		Synth::channel()->set_volume_master(master_l, master_r);
		Synth::channel()->enable_filter(true);
	}
	if(Synth::channel()->volume_sub_left() != fm_l || Synth::channel()->volume_sub_right() != fm_r) {
		PDEBUGF(LOG_V1, LOG_AUDIO, "%s Mixer: FM vol L:%.3f - R:%.3f\n", short_name(),
				fm_l, fm_r);
	}
	Synth::channel()->set_volume_sub(fm_l, fm_r);
	// TODO reg 0x6 bit 5,6 left-right routing?
}

void SBlasterPro::debug_print_volumes(uint8_t _reg, const char *_name)
{
	auto [l_level,r_level] = get_mixer_levels(_reg);
	auto [l_db,r_db] = get_mixer_volume_db(_reg);
	auto [l_fact,r_fact] = get_mixer_volume(_reg);
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s Mixer: %s = L:%d/%.0fdB/%.3f - R:%d/%.0fdB/%.3f\n", short_name(),
			_name,
			l_level, l_db, l_fact,
			r_level, r_db, r_fact
			);
}

void SBlaster::raise_interrupt()
{
	//TODO SB16 16-bit irq
	if(!m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: raising IRQ %d\n", short_name(), m_irq);
		m_devices->pic()->raise_irq(m_irq);
		m_s.pending_irq = true;
	}
}

void SBlaster::lower_interrupt()
{
	//TODO SB16 16-bit irq
	if(m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s: lowering IRQ %d\n", short_name(), m_irq);
		m_devices->pic()->lower_irq(m_irq);
		m_s.pending_irq = false;
	}
}

void SBlaster::dma_timer(uint64_t _time)
{
	// TODO distinguish 8/16 bit DMA
	if(m_s.dma.irq) {
		raise_interrupt();
		m_s.dma.irq = false;
	}
	if(m_s.dma.drq) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: requesting data\n", short_name());
		m_devices->dma()->set_DRQ(m_dma, true);
		m_s.dma.drq_time = g_machine.get_virt_time_ns();
		// What's the correct timeout? Ideal timing would be 0ns.
		g_machine.activate_timer(m_dac_timer, m_s.dac.period_ns, false);
	} else if(_time != 0) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: stopping\n", short_name());
		std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
		dsp_change_mode(DSP::Mode::NONE);
		if(m_s.dac.state != DAC::State::STOPPED) {
			dac_set_state(DAC::State::WAITING);
		}
	}
}

int SBlaster::dsp_decode(uint8_t _sample)
{
	// a lock on m_dac_mutex must be already acquired
	
	// decoding could be done by the Mixer thread, but the work needed
	// to arrange that is not worth it.
	
	if(m_s.dsp.decoder == DSP::Decoder::PCM) {
		m_s.dac.add_sample(_sample);
		return 1;
	}

	// ADPCM
	if(m_s.dsp.adpcm.have_reference) {
		m_s.dsp.adpcm.have_reference = false;
		m_s.dsp.adpcm.reference = _sample;
		m_s.dsp.adpcm.step_size = 0;
		return 0;
	}
	switch(m_s.dsp.decoder) {
		case DSP::Decoder::ADPCM2:
			m_s.dac.add_sample(dsp_decode_ADPCM2((_sample >> 6) & 0x3));
			m_s.dac.add_sample(dsp_decode_ADPCM2((_sample >> 4) & 0x3));
			m_s.dac.add_sample(dsp_decode_ADPCM2((_sample >> 2) & 0x3));
			m_s.dac.add_sample(dsp_decode_ADPCM2((_sample >> 0) & 0x3));
			return 4;
		case DSP::Decoder::ADPCM3:
			m_s.dac.add_sample(dsp_decode_ADPCM3((_sample >> 5) & 0x7));
			m_s.dac.add_sample(dsp_decode_ADPCM3((_sample >> 2) & 0x7));
			m_s.dac.add_sample(dsp_decode_ADPCM3((_sample & 0x3) << 1));
			return 3;
		case DSP::Decoder::ADPCM4:
			m_s.dac.add_sample(dsp_decode_ADPCM4((_sample >> 4) & 0xf));
			m_s.dac.add_sample(dsp_decode_ADPCM4((_sample >> 0) & 0xf));
			return 2;
			break;
		default:
			assert(false);
			return 0;
	}
}

uint16_t SBlaster::dma_read_8(uint8_t *_buffer, uint16_t _maxlen, bool)
{
	// From Memory to I/O
	// DAC
	
	m_devices->dma()->set_DRQ(m_dma, false);
	
	if(m_s.dma.mode != DMA::Mode::DMA8) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: read event with engine off\n", short_name());
		return 0;
	}
	if(!_maxlen) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: mem read buffer empty\n", short_name());
		return 0;
	}
	
	uint64_t now = g_machine.get_virt_time_ns();
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);

	double avg_rate = m_s.dac.spec.rate;
#ifdef LOG_DEBUG_MESSAGES
	assert(m_s.dac.sample_time_ns[0] < now);
	if(m_s.dac.used >= m_s.dac.spec.channels) {
		double avg_diff = double(now - m_s.dac.sample_time_ns[0]) / (m_s.dac.used / m_s.dac.spec.channels);
		avg_rate = ceil(NSEC_PER_SECOND / avg_diff);
	}
	m_s.dac.sample_time_ns[m_s.dac.used > m_s.dac.spec.channels-1] = now;
#endif

	m_s.dac.state = DAC::State::ACTIVE;
	g_machine.deactivate_timer(m_dac_timer);
	
	// Real hardware reads 1 sample at a time.
	// This function reads 1 frame at a time (1 or 2 samples).
	// Compared to reading blocks of 512 bytes, this is computationally much more expensive
	// as this func and all the DMA procedure (DRQ, HLDA) must be called hundreds of times
	// instead of only a handful.
	// But doing so is closer to real hardware and solves DAC's overflow when
	// the guest program restarts the DMA before TC.
	// A possible alternative for the DAC's overflow problem would be using audio
	// timestamps or an intermediate buffer with a timer or taking only a limited
	// amount of samples in the dac_create_samples func, but the DMA would still
	// report an incorrect count value via its status ports (don't know if it would
	// make any real world difference tho).

	float frames = 0;
	unsigned bytes = 0;
	if(m_s.dsp.decoder == DSP::Decoder::PCM) {
		do {
			m_s.dac.add_sample(_buffer[bytes++]);
			m_s.dma.left--;
		} while ((bytes < _maxlen) && (bytes < m_s.dac.spec.channels) && (m_s.dma.left != 0xffff));
		frames = float(bytes) / m_s.dac.spec.channels;
	} else {
		frames = dsp_decode(*_buffer);
		m_s.dma.left--;
		bytes = 1;
	}

	m_s.dma.drq = true;
	m_s.dma.irq = false;
	if(m_s.dma.left == 0xffff) {
		m_s.dma.irq = true;
		if(m_s.dma.autoinit) {
			m_s.dma.left = m_s.dma.count;
		} else {
			m_s.dma.drq = false;
		}
	}
	
	// calculate the time needed by the DAC to consume the produced frames then
	// fire the dma timer to terminate or request more data
	uint64_t dma_timer_ns = m_s.dac.period_ns * frames;
	uint64_t drq_time = (now - m_s.dma.drq_time);
	m_s.dma.drq_time = 0;
	if(drq_time <= dma_timer_ns) {
		dma_timer_ns -= drq_time;
	} else {
		dma_timer_ns = 0;
	}
	g_machine.activate_timer(m_dma_timer, dma_timer_ns, false);
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA8: read %u of %u bytes, frames=%.1f, left=%ub, dac buf.=%u, drq_time=%lluns, avg_rate=%.02fHz, dma_timer_ns=%lluns\n", 
			short_name(), bytes, _maxlen, frames, m_s.dma.left, m_s.dac.used, drq_time, avg_rate, dma_timer_ns);
	
	return bytes;
}

uint16_t SBlaster::dma_write_8(uint8_t *_buffer, uint16_t _maxlen, bool)
{
	// From I/O to Memory
	if(m_s.dma.mode == DMA::Mode::NONE || !_maxlen) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: write event with engine off\n", short_name());
		return 0;
	}
	
	m_devices->dma()->set_DRQ(m_dma, false);
	
	if(m_s.dma.mode == DMA::Mode::IDENTIFY) {
		assert(_maxlen);
		*_buffer = m_s.dma.identify.vadd;
		return 1;
	}
	
	// ADC
	// TODO implemented and tested only for the SB2.0 dos driver DMA initialization procedure.
	unsigned len = 0;
	do {
		_buffer[len++] = m_s.dac.silence;
		m_s.dma.left--;
	} while ((len < _maxlen) && (m_s.dma.left != 0xffff));
	
	m_s.dma.drq = true;
	m_s.dma.irq = false;
	if(m_s.dma.left == 0xffff) {
		m_s.dma.irq = true;
		if(m_s.dma.autoinit) {
			m_s.dma.left = m_s.dma.count;
		} else {
			m_s.dma.drq = false;
		}
	}
	unsigned frames = len / m_s.dac.spec.channels;
	
	uint64_t dma_timer_ns = m_s.dac.period_ns * frames;
	uint64_t drq_time = (g_machine.get_virt_time_ns() - m_s.dma.drq_time);
	m_s.dma.drq_time = 0;
	if(drq_time <= dma_timer_ns) {
		dma_timer_ns -= drq_time;
	} else {
		dma_timer_ns = 0;
	}
	g_machine.activate_timer(m_dma_timer, dma_timer_ns, false);
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA8: written %u of %u bytes, left=%u, drq_time=%lluns, timer_ns=%lluns\n", 
			short_name(), len, _maxlen, m_s.dma.left, drq_time, dma_timer_ns);
	
	return len;
}

void SBlaster::dsp_timer(uint64_t)
{
	switch(m_s.dsp.state) {
		case DSP::State::RESET:
			PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: reset complete\n", short_name());
			m_s.dsp.state = DSP::State::NORMAL;
			m_s.dsp.out.write(SB_DSP_RSTRDY);
			if(m_s.dsp.in.used) {
				dsp_read_in_buffer();
			}
			return;
		case DSP::State::RESET_START:
			break;
		case DSP::State::NORMAL:
			break;
		case DSP::State::EXEC_CMD:
			assert(m_s.dsp.cmd != SB_DSP_NOCMD);
			dsp_exec_cmd(dsp_decode_cmd(m_s.dsp.cmd));
			return;
	}
	assert(false);
}

void SBlaster::dsp_read_in_buffer()
{
	while(m_s.dsp.in.used) {
		uint8_t value = m_s.dsp.in.read();
		if(m_s.dsp.cmd == SB_DSP_NOCMD) {
			if(m_s.dsp.mode == DSP::Mode::MIDI_UART) {
				m_s.dsp.cmd_in[0] = value;
				dsp_cmd_midi_out();
				continue;
			}
			const DSPCmd *cmd = dsp_decode_cmd(value);
			if(cmd) {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: cmd 0x%02x: %s\n", short_name(), value, cmd->desc);
				m_s.dsp.cmd = value;
				m_s.dsp.cmd_len = cmd->len;
				if(!m_s.dsp.cmd_len) {
					dsp_start_cmd(cmd);
					return;
				}
			} else {
				PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: cmd 0x%02x: unknown\n", short_name(), value);
			}
		} else {
			m_s.dsp.cmd_in[m_s.dsp.cmd_in_pos] = value;
			m_s.dsp.cmd_in_pos++;
			if(m_s.dsp.cmd_in_pos >= m_s.dsp.cmd_len) {
				dsp_start_cmd(dsp_decode_cmd(m_s.dsp.cmd));
				return;
			}
		}
	}
}

void SBlaster::dsp_exec_cmd(const DSPCmd *_cmd)
{
	assert(_cmd);
	assert(m_s.dsp.state == DSP::State::EXEC_CMD);
	
	try {
		_cmd->fn(*this);
	} catch(std::exception &) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: Error executing command 0x%02x\n",
				short_name(), m_s.dsp.cmd);
	}
	
	m_s.dsp.cmd = SB_DSP_NOCMD;
	m_s.dsp.cmd_len = 0;
	m_s.dsp.cmd_in_pos = 0;
	m_s.dsp.state = DSP::State::NORMAL;
	
	if(m_s.dsp.in.used) {
		dsp_read_in_buffer();
	}
}

void SBlaster::dsp_change_mode(DSP::Mode _mode)
{
	// caller must lock the dac
	if(m_s.dsp.mode != _mode) {
		const char *modestr = "";
		switch(_mode) {
			case DSP::Mode::NONE:
				modestr = "NONE";
				// exit high speed mode if active
				m_s.dsp.high_speed = false;
				break;
			case DSP::Mode::DAC:
				modestr = "DAC";
				// only valid format is U8 mono.
				m_s.dac.change_format(AUDIO_FORMAT_U8);
				m_s.dac.spec.channels = 1;
				m_s.dac.flush_data();
				// rate is dynamic
				break;
			case DSP::Mode::DMA:
				modestr = "DMA";
				break;
			case DSP::Mode::DMA_PAUSED:
				modestr = "DMA_PAUSED";
				dma_stop();
				break;
			case DSP::Mode::MIDI_UART:
				modestr = "MIDI_UART";
				break;
		}
		PDEBUGF(LOG_V1, LOG_AUDIO, "%s DSP: mode %s\n", short_name(), modestr);
		m_s.dsp.mode = _mode;
	}
}

uint8_t SBlaster::dsp_decode_ADPCM4(uint8_t _sample)
{
	static const int8_t scaleMap[64] = {
		0,  1,  2,  3,  4,  5,  6,  7,  0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
		1,  3,  5,  7,  9, 11, 13, 15, -1,  -3,  -5,  -7,  -9, -11, -13, -15,
		2,  6, 10, 14, 18, 22, 26, 30, -2,  -6, -10, -14, -18, -22, -26, -30,
		4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60
	};
	static const uint8_t adjustMap[64] = {
		  0, 0, 0, 0, 0, 16, 16, 16,
		  0, 0, 0, 0, 0, 16, 16, 16,
		240, 0, 0, 0, 0, 16, 16, 16,
		240, 0, 0, 0, 0, 16, 16, 16,
		240, 0, 0, 0, 0, 16, 16, 16,
		240, 0, 0, 0, 0, 16, 16, 16,
		240, 0, 0, 0, 0,  0,  0,  0,
		240, 0, 0, 0, 0,  0,  0,  0
	};

	int samp = _sample + m_s.dsp.adpcm.step_size;

	if((samp < 0) || (samp > 63)) { 
		PDEBUGF(LOG_V2, LOG_AUDIO, "Bad ADPCM-4 sample\n");
		if(samp < 0 ) {
			samp =  0;
		}
		if(samp > 63) {
			samp = 63;
		}
	}

	int ref = m_s.dsp.adpcm.reference + scaleMap[samp];
	if(ref > 0xff) {
		m_s.dsp.adpcm.reference = 0xff;
	} else if(ref < 0x00) {
		m_s.dsp.adpcm.reference = 0x00;
	} else {
		m_s.dsp.adpcm.reference = uint8_t(ref & 0xff);
	}
	m_s.dsp.adpcm.step_size = (m_s.dsp.adpcm.step_size + adjustMap[samp]) & 0xff;
	
	return m_s.dsp.adpcm.reference;
}

uint8_t SBlaster::dsp_decode_ADPCM2(uint8_t _sample)
{
	static const int8_t scaleMap[24] = {
		0,  1,  0,  -1,  1,  3,  -1,  -3,
		2,  6, -2,  -6,  4, 12,  -4, -12,
		8, 24, -8, -24, 16, 48, -16, -48
	};
	static const uint8_t adjustMap[24] = {
		  0, 4,   0, 4,
		252, 4, 252, 4, 252, 4, 252, 4,
		252, 4, 252, 4, 252, 4, 252, 4,
		252, 0, 252, 0
	};

	int samp = _sample + m_s.dsp.adpcm.step_size;
	if((samp < 0) || (samp > 23)) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "Bad ADPCM-2 sample\n");
		if(samp < 0 ) {
			samp =  0;
		}
		if(samp > 23) {
			samp = 23;
		}
	}

	int ref = m_s.dsp.adpcm.reference + scaleMap[samp];
	if(ref > 0xff) {
		m_s.dsp.adpcm.reference = 0xff;
	} else if(ref < 0x00) {
		m_s.dsp.adpcm.reference = 0x00;
	} else {
		m_s.dsp.adpcm.reference = uint8_t(ref & 0xff);
	}
	m_s.dsp.adpcm.step_size = (m_s.dsp.adpcm.step_size + adjustMap[samp]) & 0xff;

	return m_s.dsp.adpcm.reference;
}

uint8_t SBlaster::dsp_decode_ADPCM3(uint8_t _sample)
{
	static const int8_t scaleMap[40] = { 
		0,  1,  2,  3,  0,  -1,  -2,  -3,
		1,  3,  5,  7, -1,  -3,  -5,  -7,
		2,  6, 10, 14, -2,  -6, -10, -14,
		4, 12, 20, 28, -4, -12, -20, -28,
		5, 15, 25, 35, -5, -15, -25, -35
	};
	static const uint8_t adjustMap[40] = {
		  0, 0, 0, 8,   0, 0, 0, 8,
		248, 0, 0, 8, 248, 0, 0, 8,
		248, 0, 0, 8, 248, 0, 0, 8,
		248, 0, 0, 8, 248, 0, 0, 8,
		248, 0, 0, 0, 248, 0, 0, 0
	};

	int samp = _sample + m_s.dsp.adpcm.step_size;
	if((samp < 0) || (samp > 39)) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "Bad ADPCM-3 sample\n");
		if(samp < 0 ) {
			samp =  0;
		}
		if(samp > 39) {
			samp = 39;
		}
	}

	int ref = m_s.dsp.adpcm.reference + scaleMap[samp];
	if(ref > 0xff) {
		m_s.dsp.adpcm.reference = 0xff;
	} else if(ref < 0x00) {
		m_s.dsp.adpcm.reference = 0x00;
	} else {
		m_s.dsp.adpcm.reference = uint8_t(ref & 0xff);
	}
	m_s.dsp.adpcm.step_size = (m_s.dsp.adpcm.step_size + adjustMap[samp]) & 0xff;

	return m_s.dsp.adpcm.reference;
}

void SBlaster::dma_start(bool _autoinit)
{
	// caller must lock dac mutex
	
	dsp_cmd_set_dma_block();
	m_s.dma.left = m_s.dma.count;
	
	//TODO use a different object for ADC
	m_s.dac.change_format(AUDIO_FORMAT_U8);
	//TODO SB16
	unsigned channels = is_stereo_mode() ? 2 : 1;
	if(m_s.dac.spec.channels != channels) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: %u channel(s)\n", short_name(), channels);
		m_s.dac.flush_data();
	}
	m_s.dac.channel = 0;
	m_s.dac.spec.channels = channels;
	dsp_update_frequency();
	
	//TODO SB16
	m_s.dma.mode = DMA::Mode::DMA8;
	m_s.dma.autoinit = _autoinit;
	
	m_s.dma.irq = false;
	m_s.dma.drq = true;
	
	dsp_change_mode(DSP::Mode::DMA);
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: started\n", short_name());
}

void SBlaster::dma_stop()
{
	if(m_s.dma.mode != DMA::Mode::NONE) {
		if(m_s.dma.drq_time) {
			// DRQ is active but data has not been written/read yet. Cancel the request.
			m_devices->dma()->set_DRQ(m_dma, false);
			m_s.dma.drq_time = 0;
		}
		g_machine.deactivate_timer(m_dma_timer);
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: stopped\n", short_name());
	}
}

void SBlaster::dsp_cmd_unimpl()
{
	PDEBUGF(LOG_V0, LOG_AUDIO, "%s DSP: Command 0x%02x not implemented\n", short_name(), m_s.dsp.cmd);
}

void SBlaster::dsp_cmd_status()
{
	m_s.dsp.out.flush();
	if( ISDSPV(2) ) {
		m_s.dsp.out.write(0x88);
	} else if( ISDSPV(3) ) {
		m_s.dsp.out.write(0x7b);
	} else {
		// Everything enabled
		m_s.dsp.out.write(0xff);
	}
}

void SBlaster::dsp_cmd_speaker_on()
{
	m_s.dac.speaker = true;
	m_dac_channel->set_force_muted(false);
}

void SBlaster::dsp_cmd_speaker_off()
{
	m_s.dac.speaker = false;
	m_dac_channel->set_force_muted(true);
}

void SBlaster::dsp_cmd_speaker_status()
{
	m_s.dsp.out.flush();
	if(m_s.dac.speaker) {
		m_s.dsp.out.write(0xff);
	} else {
		m_s.dsp.out.write(0x00);
	}
}

void SBlaster::dsp_cmd_set_time_const()
{
	m_s.dsp.time_const = m_s.dsp.cmd_in[0];
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	uint64_t old_dac_period_ns = m_s.dac.period_ns;
	dsp_update_frequency();
	if(m_s.dac.state == DAC::State::WAITING && old_dac_period_ns != m_s.dac.period_ns) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DAC: updating timer period to new value of %llu ns\n",
				short_name(), m_s.dac.period_ns);
		int64_t dac_eta = g_machine.get_timer_eta(m_dac_timer);
		int64_t new_eta = int64_t(m_s.dac.period_ns) - (int64_t(old_dac_period_ns) - dac_eta);
		if(new_eta < 0) {
			new_eta = 0;
		}
		g_machine.activate_timer(m_dac_timer, new_eta, m_s.dac.period_ns, true);
	}
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: set rate=%d, actual DAC rate=%.3f\n",
			short_name(), time_const_to_freq(m_s.dsp.time_const), m_s.dac.spec.rate);
}

void SBlaster::dsp_cmd_set_dma_block()
{
	m_s.dma.count = m_s.dsp.cmd_in[0] + (m_s.dsp.cmd_in[1] << 8);
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DMA: block size=%d bytes\n", short_name(), uint32_t(m_s.dma.count) + 1);
}

void SBlaster::dsp_cmd_direct_dac_8()
{
	// direct DAC mode doesn't have a fixed known rate, it depends on how fast
	// the program feeds the DSP.
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	
	dsp_change_mode(DSP::Mode::DAC);
	dac_set_state(DAC::State::ACTIVE);
	m_s.dsp.decoder = DSP::Decoder::PCM;
	
	uint64_t now = g_machine.get_virt_time_ns();
	assert(m_s.dac.sample_time_ns[0] < now);
	if(m_s.dac.used) {
		double avg_diff = double(now - m_s.dac.sample_time_ns[0]) / double(m_s.dac.used);
		double avg_rate = NSEC_PER_SECOND / avg_diff;
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: direct DAC avg.rate=%.2fHz\n",
				short_name(), avg_rate);
		m_s.dac.spec.rate = avg_rate;
		
		// 10 times the average rate timeout, what's the proper value tho?
		g_machine.activate_timer(m_dac_timer, avg_diff*10, false);
	}
	
	m_s.dac.sample_time_ns[m_s.dac.used>0] = now;
	m_s.dac.add_sample(m_s.dsp.cmd_in[0]);
}

void SBlaster::dsp_cmd_dma_adc(uint8_t _bits, bool _auto_init, bool _hispeed)
{
	UNUSED(_bits);
	
	m_s.dsp.high_speed = _hispeed;
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	dma_start(_auto_init);
	g_machine.deactivate_timer(m_dma_timer);
	dma_timer(0); // DRQ
	
	PDEBUGF(LOG_V1, LOG_AUDIO, "%s DSP: starting %s DMA ADC 8-bit %.2fHz\n", short_name(),
			_auto_init?"auto-init":"single cycle",
			m_s.dac.spec.rate);
}

void SBlaster::dsp_cmd_dma_dac(uint8_t _bits, bool _autoinit, bool _hispeed)
{
	switch(_bits & 0x1f) {
		case 2: m_s.dsp.decoder = DSP::Decoder::ADPCM2; break;
		case 3: m_s.dsp.decoder = DSP::Decoder::ADPCM3; break;
		case 4: m_s.dsp.decoder = DSP::Decoder::ADPCM4; break;
		case 8: m_s.dsp.decoder = DSP::Decoder::PCM; break;
		// case 16: TODO
		default: assert(false); return;
	}
	m_s.dsp.high_speed = _hispeed;
	m_s.dsp.adpcm.have_reference = _bits & REF;
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	uint64_t dma_timer_eta = 0;
	if(m_s.dac.state == DAC::State::WAITING) {
		// keep a regular flow of generated samples
		dma_timer_eta = g_machine.get_timer_eta(m_dac_timer);
	}
	
	dma_start(_autoinit);
	
	if(!g_machine.is_timer_active(m_dma_timer)) {
		if(dma_timer_eta) {
			g_machine.activate_timer(m_dma_timer, dma_timer_eta, false);
		} else {
			// DRQ
			dma_timer(0);
		}
	}
	dac_set_state(DAC::State::ACTIVE);
	
	PDEBUGF(LOG_V1, LOG_AUDIO, "%s DSP: starting %s %s DMA DAC %d-bit %s %.2fHz\n", short_name(),
			_autoinit?"auto-init":"single-cycle",
			_hispeed?"high-speed":"",
			_bits & 0x1f,
			(m_s.dsp.decoder != DSP::Decoder::PCM)?(m_s.dsp.adpcm.have_reference?"w/ref":""):"",
			m_s.dac.spec.rate);
}

void SBlaster::dsp_cmd_pause_dma_8()
{
	if(m_s.dma.mode != DMA::Mode::DMA8) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: pause DMA requested with DMA not active\n", short_name());
		return;
	}
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	dsp_change_mode(DSP::Mode::DMA_PAUSED);
	if(m_s.dac.state == DAC::State::ACTIVE) {
		dac_set_state(DAC::State::WAITING);
	}
}

void SBlaster::dsp_cmd_continue_dma_8()
{
	if(m_s.dma.mode != DMA::Mode::DMA8) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: continue DMA requested with DMA not active\n", short_name());
		return;
	}
	// DMA engine is active, so there was a timer set that has been stopped.
	// fire it now so that the DMA loop can resume.
	dma_timer(0);
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	dsp_change_mode(DSP::Mode::DMA);
	dac_set_state(DAC::State::ACTIVE);
}

void SBlaster::dsp_cmd_exit_ai_dma_8()
{
	if(m_s.dma.mode != DMA::Mode::DMA8) {
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: exit auto init while DMA not active\n", short_name());
	}
	// Exits at the end of the current 8-bit auto-init DMA block transfer
	m_s.dma.autoinit = false;
}

void SBlaster::dsp_cmd_get_version()
{
	m_s.dsp.out.flush();
	m_s.dsp.out.write(DSPVHI);
	m_s.dsp.out.write(DSPVLO);
}

void SBlaster::dsp_cmd_get_copyright()
{
	m_s.dsp.out.flush();
	for(size_t i=0; i<=strlen(SB16_COPYRIGHT); i++) {
		m_s.dsp.out.write(SB16_COPYRIGHT[i]);
	}
}

void SBlaster::dsp_cmd_pause_dac()
{
	uint32_t count = m_s.dsp.cmd_in[0] + (m_s.dsp.cmd_in[1] << 8) + 1;
	
	PDEBUGF(LOG_V2, LOG_AUDIO, "%s DSP: firing IRQ in %d samples / %llu ns\n", short_name(),
			count, (count * m_s.dac.period_ns));
	
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	m_s.dac.irq_count = count;
	if(m_s.dac.state == DAC::State::STOPPED) {
		dac_set_state(DAC::State::ACTIVE);
		dac_set_state(DAC::State::WAITING);
	}
}

void SBlaster::dsp_cmd_identify()
{
	m_s.dsp.out.flush();
	m_s.dsp.out.write(~m_s.dsp.cmd_in[0]);
}

void SBlaster::dsp_cmd_identify_dma()
{
	// DMA identification routine, reverse engineered from SB16 firmware
	// see https://github.com/joncampbell123/dosbox-x/issues/1044#issuecomment-480115593
	
	m_s.dma.identify.vadd += m_s.dsp.cmd_in[0] ^ m_s.dma.identify.vxor;
	m_s.dma.identify.vxor = (m_s.dma.identify.vxor >> 2u) | (m_s.dma.identify.vxor << 6u);
	m_s.dma.mode = DMA::Mode::IDENTIFY;
	m_devices->dma()->set_DRQ(m_dma, true);
}

void SBlaster::dsp_cmd_trigger_irq_8()
{
	raise_interrupt();
}

void SBlaster::dsp_cmd_write_test_reg()
{
	m_s.dsp.test_reg = m_s.dsp.cmd_in[0];
}

void SBlaster::dsp_cmd_read_test_reg()
{
	m_s.dsp.out.flush();
	m_s.dsp.out.write(m_s.dsp.test_reg);
}

void SBlaster::dsp_cmd_f8_unknown()
{
	m_s.dsp.out.flush();
	m_s.dsp.out.write(0);
}

void SBlaster::dsp_cmd_aux_status()
{
	// only doc found on this is http://the.earth.li/~tfm/oldpage/sb_dsp.html
	m_s.dsp.out.flush();
	m_s.dsp.out.write((!m_s.dac.speaker) | 0x12);
}

void SBlaster::dsp_cmd_midi_uart(bool _polling, bool _timestamps)
{
	dsp_change_mode(DSP::Mode::MIDI_UART);
	m_s.dsp.midi_polling = _polling;
	m_s.dsp.midi_timestamps = _timestamps;
}

void SBlaster::dsp_cmd_midi_out()
{
	g_mixer.midi()->cmd_put_byte(m_s.dsp.cmd_in[0], g_machine.get_virt_time_ns());
}

void SBlaster::DSP::DataBuffer::flush()
{
	used = 0;
	pos = 0;
}

void SBlaster::DSP::DataBuffer::write(uint8_t _data)
{
	if(used < BUFSIZE) {
		unsigned start = used + pos;
		if(start >= BUFSIZE) {
			start -= BUFSIZE;
		}
		data[start] = _data;
		used++;
	}
}

uint8_t SBlaster::DSP::DataBuffer::read()
{
	if(used) {
		lastval = data[pos];
		pos++;
		if(pos >= BUFSIZE) {
			pos -= BUFSIZE;
		}
		used--;
	}
	return lastval;
}

void SBlaster::DAC::flush_data()
{
	// caller must lock dac mutex
	used = 0;
	sample_time_ns[0] = 0;
	sample_time_ns[1] = 0;
	channel = 0;
}

void SBlaster::DAC::add_sample(uint8_t _sample)
{
	// caller must lock dac mutex
	if(used < BUFSIZE) {
		data[used] = _sample;
		used++;
	}
	if(spec.channels == 1) {
		last_value[0] = _sample;
	} else {
		last_value[channel] = _sample;
		channel = 1 - channel;
	}
	if(irq_count) {
		irq_count--;
		if(irq_count == 0) {
			device->raise_interrupt();
		}
	}
}

void SBlaster::DAC::change_format(AudioFormat _format)
{
	if(_format == AUDIO_FORMAT_U8) {
		spec.format = _format;
		silence = 128;
	} else if(_format == AUDIO_FORMAT_S16) {
		spec.format = _format;
		silence = 0;
	} else {
		PDEBUGF(LOG_V0, LOG_AUDIO, "invalid sample format\n");
	}
}

void SBlaster::dac_set_state(DAC::State _to_state)
{
	// caller must lock dac mutex
	
	switch(_to_state) {
		case DAC::State::ACTIVE:
			if(m_s.dac.state == DAC::State::STOPPED) {
				g_machine.deactivate_timer(m_dac_timer);
				m_dac_channel->enable(true);
				m_s.dac.flush_data();
				m_s.dac.newdata = true;
				m_s.dac.last_value[0] = m_s.dac.last_value[1] = m_s.dac.silence;
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: activated\n", short_name());
				break;
			} else if(m_s.dac.state == DAC::State::WAITING) {
				// dac is generating samples, stop it
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: reactivated\n", short_name());
				g_machine.deactivate_timer(m_dac_timer);
				break;
			}
			break;
		case DAC::State::WAITING:
			m_s.dac.sample_time_ns[0] = g_machine.get_virt_time_ns();
			// start generating samples now, no delay
			g_machine.activate_timer(m_dac_timer, 0, m_s.dac.period_ns, true);
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: waiting, cycle period=%lluns\n", short_name(), m_s.dac.period_ns);
			break;
		case DAC::State::STOPPED:
			if(m_s.dac.state != DAC::State::STOPPED) {
				g_machine.deactivate_timer(m_dac_timer);
				// don't disable che channel, the Mixer is responsible for that.
				// samples that are already in the DAC buffer will continue to play.
				PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: deactivated\n", short_name());
			}
			break;
	}
	m_s.dac.state = _to_state;
}

void SBlaster::dsp_update_frequency()
{
	// caller must lock the dac mutex
	
	// TODO SB16 these limits (and stereo mode) are valid only for DSP ver. <= 3.xx
	
	uint8_t tc = m_s.dsp.time_const;
	
	uint8_t hilimit = SB_TC_45454;
	uint8_t lolimit = SB_TC_4000;
	
	switch(m_s.dsp.decoder) {
		case DSP::Decoder::PCM:    hilimit = SB_TC_45454; break;
		case DSP::Decoder::ADPCM2: hilimit = SB_TC_11000; break;
		case DSP::Decoder::ADPCM3: hilimit = SB_TC_13000; break;
		case DSP::Decoder::ADPCM4: hilimit = SB_TC_12000; break;
	}
	
	if(!m_s.dsp.high_speed) {
		hilimit = SB_TC_23000;
	}
	
	tc = std::min(hilimit, tc);
	tc = std::max(lolimit, tc);

	uint16_t freq = time_const_to_freq(tc) / m_s.dac.spec.channels;

	double old_rate = m_s.dac.spec.rate;
	// Calculate an integer sample period in ns and derive a sample rate from it.
	m_s.dac.period_ns = round(1e9 / double(freq));
	m_s.dac.spec.rate = 1e9 / double(m_s.dac.period_ns);
	m_s.dac.timeout_ns = SB_DAC_TIMEOUT;
	
	if(m_s.dac.spec.rate != old_rate) {
		PDEBUGF(LOG_V1, LOG_AUDIO, "%s DSP: old rate=%.3f Hz, new rate=%.3f Hz, period=%llu ns\n", short_name(),
				old_rate, m_s.dac.spec.rate, m_s.dac.period_ns);
		if(m_s.dac.used) {
			// DAC will have no data in case of switching mono-stereo mode
			static std::array<uint8_t,DAC::BUFSIZE> tempbuf;
			size_t generated = 0;
			if(m_s.dac.spec.channels == 1) {
				generated = Audio::Convert::resample_mono<uint8_t>(
						m_s.dac.data, m_s.dac.used, old_rate, &tempbuf[0], DAC::BUFSIZE, m_s.dac.spec.rate);
			} else {
				if(m_s.dac.used & 1) {
					PDEBUGF(LOG_V0, LOG_AUDIO, "%s DSP: unexpected number of samples in stereo mode: %u\n", short_name(),
							m_s.dac.used);
					m_s.dac.used--;
					if(!m_s.dac.used) {
						return;
					}
				}
				unsigned frames = m_s.dac.used / 2;
				generated = Audio::Convert::resample_stereo<uint8_t>(
						m_s.dac.data, frames, old_rate, &tempbuf[0], DAC::BUFSIZE, m_s.dac.spec.rate);
			}
			memcpy(m_s.dac.data, &tempbuf[0], generated);
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: resampled %u samples at %.3f Hz, to %zu samples at %.3f Hz\n",
					short_name(), m_s.dac.used, old_rate, generated, m_s.dac.spec.rate);
			m_s.dac.used = generated;
		}
	}
}

void SBlaster::dac_timer(uint64_t)
{
	std::lock_guard<std::mutex> dac_lock(m_dac_mutex);
	
	if(m_s.dac.state == DAC::State::WAITING) {
		m_s.dac.add_sample(m_s.dac.last_value[0]);
		if(m_s.dac.spec.channels == 2) {
			m_s.dac.add_sample(m_s.dac.last_value[1]);
		}
		PDEBUGF(LOG_V2, LOG_AUDIO, "%s DAC: adding fills\n", short_name());
		if(!m_s.dac.irq_count && (g_machine.get_virt_time_ns() - m_s.dac.sample_time_ns[0]) > m_s.dac.timeout_ns) {
			PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: timeout expired\n", short_name());
			dac_set_state(DAC::State::STOPPED);
		}
	} else {
		PDEBUGF(LOG_V1, LOG_AUDIO, "%s DAC: timeout expired\n", short_name());
		dac_set_state(DAC::State::STOPPED);
	}
}

//this method is called by the Mixer thread
bool SBlaster::dac_create_samples(uint64_t _time_span_ns, bool, bool)
{
	// TODO SB16
	// everything here assumes u8 sample data type.

	m_dac_mutex.lock();

	uint64_t mtime_ns = g_machine.get_virt_time_ns_mt();
	unsigned pre_frames = 0, post_frames = 0;
	unsigned dac_frames = m_s.dac.spec.samples_to_frames(m_s.dac.used);
	double needed_frames = ns_to_frames(_time_span_ns, m_s.dac.spec.rate);
	bool chactive = true;
	static double balance = 0.0;

	m_dac_channel->set_in_spec(m_s.dac.spec);

	if(m_s.dac.newdata) {
		balance = 0.0;
	}

	if(m_s.dac.newdata && (dac_frames < needed_frames)) {
		pre_frames = needed_frames - dac_frames;
		m_dac_channel->in().fill_frames<uint8_t>(pre_frames, m_s.dac.last_value);
		balance += pre_frames;
	}

	if(dac_frames > 0) {
		unsigned samples = dac_frames * m_s.dac.spec.channels;
		m_dac_channel->in().add_samples(m_s.dac.data, samples);
		m_s.dac.used -= samples;
		if(m_s.dac.used) {
			memmove(m_s.dac.data, m_s.dac.data + samples, m_s.dac.used);
		}
		m_dac_channel->set_disable_time(mtime_ns);
		balance += dac_frames;
	}

	balance -= needed_frames;
	
	if(m_s.dac.state == DAC::State::STOPPED && (balance <= 0) && pre_frames==0) {
		chactive = !m_dac_channel->check_disable_time(mtime_ns);
		post_frames = balance * -1.0;
		m_dac_channel->in().fill_samples<uint8_t>(post_frames*m_s.dac.spec.channels, m_s.dac.silence);
		m_s.dac.last_value[0] = m_s.dac.last_value[1] = m_s.dac.silence;
		balance += post_frames;
	}
	
	unsigned total = pre_frames + dac_frames + post_frames;
	PDEBUGF(LOG_V2, LOG_MIXER, "%s DAC: update: %04llu ns, %.2f needed frames at %.2f Hz, rendered %d+%d+%d (%.2f us), balance=%.2f\n",
			short_name(), _time_span_ns, needed_frames, m_s.dac.spec.rate,
			pre_frames, dac_frames, post_frames,
			frames_to_us(total, m_s.dac.spec.rate),
			balance);
	
	m_s.dac.newdata &= (dac_frames == 0);
	m_dac_mutex.unlock();

	m_dac_channel->input_finish();

	return chactive;
}