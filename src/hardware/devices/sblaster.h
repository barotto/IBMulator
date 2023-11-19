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

/* Sound Blaster family of audio cards by Creative Labs.
 * 
 * Currently implemented:
 * - Sound Blaster 1.5 (22KHz 8bit mono, OPL2, DSP 1.05)
 * - Sound Blaster Pro (22KHz 8bit stereo or 44KHz 8bit mono, dual OPL2, DSP 3.00)
 * - Sound Blaster 2.0 (44KHz 8bit mono, OPL2, DSP 2.01)
 * - Sound Blaster Pro 2 (22KHz 8bit stereo or 44KHz 8bit mono, OPL3, DSP 3.02)
 * 
 * TODO:
 * - Sound Balster 16 (44KHz 16bit stereo, OPL3, DSP 4.05)
 * 
 * Sources of info used for implementation:
 * - Sound Blaster Series Hardware Programming Guide, by Creative Technology Ltd.
 * - Programming the AdLib/Sound Blaster FM Music Chips, by Jeffrey S. Lee
 * - Programmer's Guide to Yamaha YMF 262/OPL3 FM Music Synthesizer, by Vladimir Arnost
 * - Sound Blaster Page, by TFM (http://the.earth.li/~tfm/oldpage/sb.html)
 * - DOSBox, by The DOSBox Team (src/hardware/sblaster.cpp)
 * - DOSBox-X, by Jonathan Campbell (src/hardware/sblaster.cpp)
 * - DOSBox-X Wiki, by Jonathan Campbell (https://github.com/joncampbell123/dosbox-x/wiki)
 * - Bochs, by The Bochs Project (iodev/sound/sb16.cc)
 * - Sound Blaster Programming Information v0.90 by André Baresel - Craig Jackson
 */

#ifndef IBMULATOR_HW_SBLASTER_H
#define IBMULATOR_HW_SBLASTER_H

#include "hardware/iodevice.h"
#include "mixer.h"
#include "opl.h"
#include "audio/synth.h"


class SBlaster : public IODevice, protected Synth
{
	IODEVICE(SBlaster, "Sound Blaster");

public:
	enum SBlasterType {
		SB1    = 1,
		SBPRO1 = 2,
		SB2    = 3,
		SBPRO2 = 4 
	};
	
protected:
	unsigned m_iobase = 0;
	unsigned m_irq = 0;
	unsigned m_dma = 0;
	std::string m_blaster_env;
	
	OPL m_OPL[2];
	
	struct DSP {
		constexpr static int BUFSIZE = 64;
		enum State {
			RESET_START, RESET, NORMAL, EXEC_CMD
		};
		enum Mode {
			NONE, DAC, DMA, DMA_PAUSED, MIDI_UART
		};
		enum Decoder {
			PCM, ADPCM2, ADPCM3, ADPCM4
		};
		State state;
		Mode  mode;
		Decoder decoder;
		
		uint8_t time_const; // the time constant as set by the guest, the DAC/ADC rate could be different
		bool high_speed;
		bool midi_polling;
		bool midi_timestamps;
		
		uint8_t cmd;
		uint8_t cmd_len;
		uint8_t cmd_in_pos;
		uint8_t cmd_in[BUFSIZE];

		struct DataBuffer {
			uint8_t lastval;
			uint8_t data[BUFSIZE];
			uint8_t pos;
			uint8_t used;
			
			void flush();
			void write(uint8_t _data);
			uint8_t read();
		} in, out;

		struct {
			bool have_reference;
			uint8_t reference;
			int step_size;
		} adpcm;
		uint8_t test_reg;
	};
	
	struct DMA {
		enum Mode {
			NONE, DMA8, IDENTIFY
		};
		Mode mode;
		uint16_t count;
		uint16_t left;
		uint64_t drq_time;
		bool drq;
		bool irq;
		bool autoinit;
		struct {
			uint8_t vadd;
			uint8_t vxor;
		} identify;
	};
	
	// IBMulator's Mixer-Machine threads sync object
	// Prior to accessing this object a lock on m_dac_mutex must be acquired.
	struct DAC {
		constexpr static int BUFSIZE = 4096;
		enum State {
			ACTIVE, WAITING, STOPPED
		};
		State state;
		// TODO use s16 for SB16
		uint8_t data[BUFSIZE + 1];
		unsigned used;
		uint64_t sample_time_ns[2];
		uint8_t last_value[2];
		uint16_t silence;
		bool newdata;
		uint64_t period_ns;
		uint64_t timeout_ns;
		AudioSpec spec;
		bool speaker;
		uint32_t irq_count;
		SBlaster *device;
		unsigned channel;

		void flush_data();
		void add_sample(uint8_t _sample);
		void change_format(AudioFormat _format);
	};

	struct Mixer {
		uint8_t reg_idx;
		uint8_t reg[256];
	};
	
	struct State {
		struct {
			uint8_t reg_port[2];
			uint8_t reg[2];
		} opl;
		DSP dsp;
		DMA dma;
		DAC dac;
		Mixer mixer;
		bool pending_irq;
	} m_s;

	int m_dsp_ver = 0x0;

	struct DSPCmd {
		unsigned dsp_vmask;
		unsigned len;
		unsigned time_us;
		std::function<void(SBlaster&)> fn;
		const char *desc;
	};
	static const std::multimap<int, DSPCmd> ms_dsp_commands;

	std::mutex m_dac_mutex;
	std::shared_ptr<MixerChannel> m_dac_channel;

	std::string m_dac_filters;
	std::string m_opl_filters;

	TimerID m_dsp_timer = NULL_TIMER_ID;
	TimerID m_dma_timer = NULL_TIMER_ID;
	TimerID m_dac_timer = NULL_TIMER_ID;

	std::mutex m_volume_mutex;

public:
	SBlaster(Devices *_dev);
	virtual ~SBlaster() {}

	virtual unsigned type() const { return SB1; }
	bool is(unsigned _type) { return type() == _type; }
	virtual const char *full_name() { return "Sound Blaster 1.5"; }
	virtual const char *short_name() { return "SB1"; }
	const char *blaster_env();
	
	virtual void install();
	virtual void remove();
	
	void reset(unsigned _type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

protected:
	void install_ports(const IODevice::IOPorts &_ports);
	void install_dsp(int _version, std::string _filters);
	void install_opl(OPL::ChipType _type, int _count, bool _mixer, std::string _filters);

	virtual uint16_t read_fm(uint16_t _address);
	virtual uint16_t read_dsp(uint16_t _address);
	virtual uint16_t read_mixer(uint16_t _address);
	
	virtual void write_fm(uint16_t _address, uint16_t _value);
	virtual void write_dsp(uint16_t _address, uint16_t _value);
	virtual void write_mixer(uint16_t _address, uint16_t _value);
	
	void write_fm(uint8_t _chip, uint16_t _address, uint16_t _value);
	
	void raise_interrupt();
	void lower_interrupt();

	virtual void mixer_reset() {}
	virtual void update_volumes();
	virtual bool is_stereo_mode() {
		return m_s.mixer.reg[0x0E] & 0b10;
	}

	void dsp_reset();
	void dsp_read_in_buffer();
	void dsp_change_mode(DSP::Mode _mode);
	void dsp_start_cmd(const DSPCmd *);
	void dsp_exec_cmd(const DSPCmd *);
	void dsp_timer(uint64_t);
	const DSPCmd * dsp_decode_cmd(uint8_t _cmd);
	int dsp_decode(uint8_t _sample);
	uint8_t dsp_decode_ADPCM4(uint8_t _sample);
	uint8_t dsp_decode_ADPCM3(uint8_t _sample);
	uint8_t dsp_decode_ADPCM2(uint8_t _sample);
	void dsp_update_frequency();
	
	void dsp_cmd_unimpl();
	void dsp_cmd_status();
	void dsp_cmd_set_time_const();
	void dsp_cmd_set_dma_block();
	void dsp_cmd_direct_dac_8();
	void dsp_cmd_dma_dac(uint8_t _bits, bool _autoinit, bool _hispeed);
	void dsp_cmd_dma_adc(uint8_t _bits, bool _autoinit, bool _hispeed);
	void dsp_cmd_pause_dma_8();
	void dsp_cmd_continue_dma_8();
	void dsp_cmd_exit_ai_dma_8();
	void dsp_cmd_speaker_on();
	void dsp_cmd_speaker_off();
	void dsp_cmd_speaker_status();
	void dsp_cmd_get_version();
	void dsp_cmd_get_copyright();
	void dsp_cmd_pause_dac();
	void dsp_cmd_identify();
	void dsp_cmd_identify_dma();
	void dsp_cmd_trigger_irq_8();
	void dsp_cmd_write_test_reg();
	void dsp_cmd_read_test_reg();
	void dsp_cmd_f8_unknown();
	void dsp_cmd_aux_status();
	void dsp_cmd_midi_uart(bool _polling, bool _timestamps);
	void dsp_cmd_midi_out();
	
	void dac_timer(uint64_t);
	bool dac_create_samples(uint64_t _time_span_us, bool, bool);
	void dac_set_state(DAC::State _to_state);
	
	void dma_start(bool _autoinit);
	void dma_stop();
	void dma_timer(uint64_t);
	uint16_t dma_write_8(uint8_t *_buffer, uint16_t _maxlen, bool);
	uint16_t dma_read_8(uint8_t *_buffer, uint16_t _maxlen, bool);

	// Mixer callback functions
	void auto_filter_cb(MixerChannel *_ch, std::string _filter);
	void auto_resampling_cb();
};

class SBlaster2 : public SBlaster
{
public:
	SBlaster2(Devices *_dev) : SBlaster(_dev) {}
	~SBlaster2() {}
	
	unsigned type() const { return SB2; }
	const char *full_name() { return "Sound Blaster 2.0"; }
	const char *short_name() { return "SB2"; }

	void install();

protected:
	void write_fm(uint16_t _address, uint16_t _value);
};

class SBlasterPro : public SBlaster
{
public:
	SBlasterPro(Devices *_dev) : SBlaster(_dev) {}
	virtual ~SBlasterPro() {}

protected:
	uint16_t read_mixer(uint16_t _address);
	void write_mixer(uint16_t _address, uint16_t _value);

	void mixer_reset();
	void update_volumes();

	std::pair<int,int> get_mixer_levels(uint8_t _reg);
	std::pair<float,float> get_mixer_volume_db(uint8_t _reg);
	std::pair<float,float> get_mixer_volume(uint8_t _reg);
	void debug_print_volumes(uint8_t _reg, const char *_name);
};

class SBlasterPro1 : public SBlasterPro
{
public:
	SBlasterPro1(Devices *_dev) : SBlasterPro(_dev) {}
	~SBlasterPro1() {}
	
	unsigned type() const { return SBPRO1; }
	const char *full_name() { return "Sound Blaster Pro"; }
	const char *short_name() { return "SBPro1"; }
	
	void install();
	
protected:
	void write_fm(uint16_t _address, uint16_t _value);
	uint16_t read_fm(uint16_t _address);
};

class SBlasterPro2 : public SBlasterPro
{
public:
	SBlasterPro2(Devices *_dev) : SBlasterPro(_dev) {}
	~SBlasterPro2() {}
	
	unsigned type() const { return SBPRO2; }
	const char *full_name() { return "Sound Blaster Pro 2"; }
	const char *short_name() { return "SBPro2"; }
	
	void install();

protected:
	void write_fm(uint16_t _address, uint16_t _value);
};

#endif
