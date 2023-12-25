/*
 * Copyright (C) 2023  Marco Bortolin
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

#ifndef IBMULATOR_GUI_MIXER_CONTROL_H
#define IBMULATOR_GUI_MIXER_CONTROL_H

#include "window.h"
#include "mixer_save_info.h"

class Mixer;
class MixerChannel;
class GUI;

class MixerControl : public Window
{
private:
	Mixer *m_mixer;
	struct {
		Rml::Element *audiocards_channels, *soundfx_channels;
	} m_divs = {};
	struct Channel {
		std::shared_ptr<MixerChannel> ch;
		int id = -1;
		Rml::Element *activity = nullptr;
		Rml::Element *vol_slider = nullptr;
		Rml::Element *vol_progress = nullptr;
		Rml::Element *vol_value = nullptr;
		Rml::Element *vu_left = nullptr;
		Rml::Element *vu_right = nullptr;
		Rml::Element *volume_auto_btn = nullptr;
		Rml::Element *filter_en_check = nullptr;
		float vol_last_value = 0.f;

		Channel() {}
		Channel(std::shared_ptr<MixerChannel> _ch, Rml::ElementDocument *_wnd);

		void set(int _id, Rml::ElementDocument *_wnd);
	};
	std::map<int,Channel> m_channels;
	std::map<AppConfig::ConfigPair, std::vector<MixerChannel*>> m_ch_links;
	TimerID m_click_timer = NULL_TIMER_ID;
	std::function<void()> m_click_cb;
	std::unique_ptr<MixerSaveInfo> m_save_info;
	bool m_vu_meters = true;
	static constexpr float ms_max_volume = MIXER_MAX_VOLUME * 100.f;

	static event_map_t ms_evt_map;

public:
	MixerControl(GUI *_gui, Mixer *_mixer);

	virtual void create();
	virtual void update();
	virtual void config_changed(bool);
	virtual void close();

	event_map_t & get_event_map() { return MixerControl::ms_evt_map; }

private:
	void update_vu_meter(Rml::Element *meter, double db);

	void on_save(Rml::Event &);
	void on_vu_meters(Rml::Event &);
	void enable_vu_meters(bool _enabled);

	bool m_is_sliding = false;
	void on_slider_dragstart(Rml::Event &);
	void on_slider_dragend(Rml::Event &);

	template< typename T >
	void set_control_value(Rml::Element *_control, const T &_value, std::string _attr = "value") {
		enable_handlers(false);
		_control->SetAttribute(_attr, _value);
		enable_handlers(true);
	}

	void remove_control_attr(Rml::Element *_control, std::string _attr) {
		enable_handlers(false);
		_control->RemoveAttribute(_attr);
		enable_handlers(true);
	}

	void init_channel_values(MixerChannel *_ch);

	void set_volume(int _id, float _value);
	void set_volume_slider(int _id, float _master);
	void set_volume_label(int _id, float _master);
	void set_volume_label(int _id, float _left, float _right);
	bool on_volume_change(Rml::Event &, int _chid);
	bool on_volume_auto(Rml::Event &, int _chid);

	void set_balance(int _id, float _value, bool _update_links = false);
	void set_balance_slider(int _id, float _value);
	void set_balance_label(int _id, float _value);
	bool on_balance_change(Rml::Event &, int _chid);

	void set_mute(int _chid, bool _val);
	void set_solo(int _chid, bool _val);
	bool on_mute(Rml::Event &, int _chid);
	bool on_solo(Rml::Event &, int _chid);

	bool on_setting(Rml::Event &, int _chid);

	void set_filter(int _chid, std::string _preset);
	void add_filter(int _chid, const Dsp::Filter *_filter, size_t _filter_idx);
	void update_filter_chain(int _chid);
	bool on_filter_preset(Rml::Event &, int _chid);
	bool on_filter_change(Rml::Event &, int _chid, size_t _filter_idx);
	bool on_filter_add(Rml::Event &, int _chid);
	bool on_filter_remove(Rml::Event &, int _chid, size_t _filter_idx);
	bool on_filter_enable(Rml::Event &, int _chid);
	bool on_filter_setting(Rml::Event &, int _chid);
	void incdec_filter_param(Rml::Element *_spinner, Dsp::ParamID _param_id, int _chid, int _dspid, bool _increase);

	Rml::ElementPtr create_spinner(std::string _param_name, Dsp::ParamID _param_id, double _value, int _chid, int _dspid);
	bool on_spinner_btn(Rml::Event &, Rml::Element *_spinner, Dsp::ParamID _param_id, int _chid, int _dspid, bool _increase);
	void set_spinner_value(Rml::Element *_spinner, double _value);

	bool on_reverb_preset(Rml::Event &, int _chid);
	bool on_chorus_preset(Rml::Event &, int _chid);
	bool on_crossfeed_preset(Rml::Event &, int _chid);
	bool on_resampling_mode(Rml::Event &, int _chid);

	Rml::ElementPtr create_master_block();
	Rml::ElementPtr create_category_block(MixerChannel::Category _id);

	Rml::ElementPtr create_channel_block(const MixerChannel *_ch);
	Rml::ElementPtr create_AMS_buttons(int _id, bool _auto, bool _mute, bool _solo);
	Rml::ElementPtr create_volume_slider(int _id);
	Rml::ElementPtr create_vu_meter(int _id, bool _stereo);
	Rml::ElementPtr create_balance_slider(int _id);
	Rml::ElementPtr create_filters_setting(int _id, bool _has_auto);
	Rml::ElementPtr create_filters_container(int _id, bool _has_auto);
	Rml::ElementPtr create_reverb_setting(int _id, bool _has_auto);
	Rml::ElementPtr create_chorus_setting(int _id, bool _has_auto);
	Rml::ElementPtr create_crossfeed_setting(int _id);
	Rml::ElementPtr create_resampling_setting(int _id, bool _has_auto);
};

#endif
