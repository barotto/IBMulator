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

#include "ibmulator.h"
#include "program.h"
#include "gui.h"
#include "mixer.h"
#include "mixer_control.h"
#include <RmlUi/Core.h>

event_map_t MixerControl::ms_evt_map = {
	GUI_EVT( "class:ch_volume_slider", "dragstart", MixerControl::on_slider_dragstart ),
	GUI_EVT( "class:ch_volume_slider", "dragend", MixerControl::on_slider_dragend ),
	GUI_EVT( "close", "click", Window::on_cancel ),
	GUI_EVT( "*", "keydown", Window::on_keydown )
};

using namespace std::placeholders;

MixerControl::MixerControl(GUI *_gui, Mixer *_mixer)
:
Window(_gui, "mixer_control.rml"),
m_mixer(_mixer)
{
}

void MixerControl::create()
{
	Window::create();

	m_divs.audiocards_channels = get_element("audiocards_channels");
	m_divs.soundfx_channels = get_element("soundfx_channels");

	get_element("master")->AppendChild(create_master_block());
	get_element("audiocards")->AppendChild(create_category_block(MixerChannel::AUDIOCARD));
	get_element("soundfx")->AppendChild(create_category_block(MixerChannel::SOUNDFX));

	m_divs.master_vol = get_element("ch_vol_-1");
	m_divs.audiocards_vol = get_element(str_format("ch_vol_%d", MixerChannel::AUDIOCARD));

	m_divs.master_vol_progress = get_element("ch_vol_progress_-1");
	m_divs.audiocards_vol_progress = get_element(str_format("ch_vol_progress_%d", MixerChannel::AUDIOCARD));

	m_click_timer = m_gui->timers().register_timer([this](uint64_t){
		if(!m_click_cb) {
			m_gui->timers().deactivate_timer(m_click_timer);
		} else {
			m_click_cb();
		}
	}, "Filter Parameter click");
}

void MixerControl::update()
{
	for(auto & [chid, ch] : m_channels) {
		if(ch.ch->is_enabled() || ch.ch->out().frames() || ch.ch->in().frames()) {
			ch.activity->SetClass("enabled", true);
		} else {
			ch.activity->SetClass("enabled", false);
		}

		// TODO? this garbage exists because we're lacking a locking system in the GUI required to
		// use MixerChannel's callbacks (see MixerChannel::add_parameter_cb())
		// voglio morire
		if(ch.ch->is_volume_auto()) {
			if(ch.ch->features() & MixerChannel::HasStereoSource) {
				set_volume_slider(chid, (ch.ch->volume_master_left() + ch.ch->volume_master_right()) / 2.0);
				set_volume_label(chid, ch.ch->volume_master_left(), ch.ch->volume_master_right());
			} else {
				set_volume_slider(chid, ch.ch->volume_master_left());
				set_volume_label(chid, ch.ch->volume_master_left());
			}
			if(ch.ch->features() & MixerChannel::HasFilter) {
				auto check = get_element(str_format("ch_filter_en_%d", chid));
				set_control_value(check, ch.ch->is_filter_enabled(), "checked");
			}
		}
		if(!m_is_sliding) {
			float val = m_divs.master_vol->GetAttribute("real_value")->Get(1.f);
			if(val != m_mixer->volume_master()) {
				set_volume_slider(m_divs.master_vol, m_divs.master_vol_progress, -1, m_mixer->volume_master());
			}
			val = m_divs.audiocards_vol->GetAttribute("real_value")->Get(1.f);
			if(val != m_mixer->volume_cat(MixerChannel::AUDIOCARD)) {
				set_volume_slider(m_divs.audiocards_vol, m_divs.audiocards_vol_progress, MixerChannel::AUDIOCARD, m_mixer->volume_cat(MixerChannel::AUDIOCARD));
			}
		}
	}
}

void MixerControl::config_changed(bool)
{
	if(!m_wnd) {
		create();
	}

	m_divs.audiocards_channels->SetInnerRML("");
	m_divs.soundfx_channels->SetInnerRML("");

	m_channels.clear();
	m_ch_links.clear();

	size_t audioc_count = 0;
	size_t sfx_count = 0;

	for(auto &ch : m_mixer->get_channels(MixerChannel::AUDIOCARD)) {
		if(!ch->features()) {
			continue;
		}
		m_divs.audiocards_channels->AppendChild(create_channel_block(ch.get()));
		m_channels[ch->id()] = { ch, nullptr };
		init_channel_values(ch.get());
		audioc_count++;
	}

	for(auto &ch : m_mixer->get_channels(MixerChannel::SOUNDFX)) {
		if(!ch->features()) {
			continue;
		}
		m_divs.soundfx_channels->AppendChild(create_channel_block(ch.get()));
		m_channels[ch->id()] = { ch, nullptr };
		init_channel_values(ch.get());
		sfx_count++;
	}

	for(auto &ch : m_channels) {
		ch.second.activity = get_element(str_format("ch_name_%d", ch.first));
	}

	set_volume_slider(-1, m_mixer->volume_master());
	set_volume_slider(MixerChannel::AUDIOCARD, m_mixer->volume_cat(MixerChannel::AUDIOCARD));
	set_volume_slider(MixerChannel::SOUNDFX, m_mixer->volume_cat(MixerChannel::SOUNDFX));

	auto reverb_preset = get_element(str_format("ch_reverb_preset_%d", MixerChannel::SOUNDFX));
	if(!m_mixer->is_reverb_enabled(MixerChannel::SOUNDFX)) {
		set_control_value(reverb_preset, "none");
	} else {
		set_control_value(reverb_preset, MixerChannel::reverb_preset_to_config(m_mixer->reverb(MixerChannel::SOUNDFX).preset).name);
	}

	auto block = get_element("audiocards");
	Rml::Vector2<float> block_size = block->GetBox().GetSize();
	float margin = block->GetProperty(Rml::PropertyId::MarginLeft)->Get<float>();
	margin += block->GetProperty(Rml::PropertyId::MarginRight)->Get<float>();
	float block_w_dp = block_size.x / m_gui->scaling_factor() + margin;

	auto left = get_element("channels")->GetProperty(Rml::PropertyId::Left)->Get<float>();
	auto right = get_element("channels")->GetProperty(Rml::PropertyId::Right)->Get<float>();
	left += get_element("audiocards_channels")->GetProperty(Rml::PropertyId::Left)->Get<float>();

	float max_w_dp = left;
	if(audioc_count > sfx_count) {
		max_w_dp += audioc_count * block_w_dp;
	} else {
		max_w_dp += sfx_count * block_w_dp;
	}
	max_w_dp += right;

	m_wnd->SetProperty("max-width", str_format("%gdp", max_w_dp));
}

void MixerControl::init_channel_values(MixerChannel *_ch)
{
	set_volume_slider(_ch->id(), _ch->volume_master_left());

	if(_ch->is_volume_auto()) {
		set_active(get_element(str_format("ch_volume_auto_%d", _ch->id())), true);
		set_disabled(get_element(str_format("ch_vol_%d", _ch->id())), true);
		if(_ch->features() & MixerChannel::HasStereoSource) {
			set_volume_label(_ch->id(), _ch->volume_master_left(), _ch->volume_master_right());
		} else {
			set_volume_label(_ch->id(), _ch->volume_master_left());
		}
		if(_ch->features() & MixerChannel::Feature::HasAutoFilter) {
			auto check = get_element(str_format("ch_filter_en_%d", _ch->id()));
			set_disabled(check, true);
		}
	} else {
		set_volume_slider(_ch->id(), _ch->volume_master_left());
	}

	set_balance_slider(_ch->id(), _ch->balance());

	if(_ch->category() == MixerChannel::SOUNDFX && (_ch->features() & MixerChannel::HasBalance)) {
		auto cfgpair = _ch->config_map().at(MixerChannel::ConfigParameter::Balance);
		m_ch_links[cfgpair].push_back(_ch);
	}

	if(_ch->features() & MixerChannel::Feature::HasFilter) {
		if(_ch->is_filter_enabled()) {
			auto filter_enabled = get_element(str_format("ch_filter_en_%d", _ch->id()));
			set_control_value(filter_enabled, 1, "checked");
		}
		auto filter_preset = get_element(str_format("ch_filter_preset_%d", _ch->id()));
		if(!_ch->is_filter_set()) {
			set_control_value(filter_preset, "none");
		} else if(_ch->is_filter_auto()) {
			set_control_value(filter_preset, "auto");
		} else {
			if(_ch->filter().preset == MixerChannel::FilterPreset::Custom) {
				set_control_value(filter_preset, "custom");
				auto custom_container = get_element(str_format("ch_filter_custom_%d", _ch->id()));
				custom_container->SetClass("d-none", false);
				update_filter_chain(_ch->id());
			} else {
				set_control_value(filter_preset, _ch->filter().name);
			}
		}
	}

	if(_ch->features() & MixerChannel::Feature::HasReverb) {
		auto reverb_preset = get_element(str_format("ch_reverb_preset_%d", _ch->id()));
		if(!_ch->is_reverb_enabled()) {
			set_control_value(reverb_preset, "none");
		} else if(_ch->is_reverb_auto()) {
			set_control_value(reverb_preset, "auto");
		} else {
			set_control_value(reverb_preset, MixerChannel::reverb_preset_to_config(_ch->reverb().preset).name);
		}
	}

	if(_ch->features() & MixerChannel::Feature::HasChorus) {
		auto chorus_preset = get_element(str_format("ch_chorus_preset_%d", _ch->id()));
		if(!_ch->is_chorus_enabled()) {
			set_control_value(chorus_preset, "none");
		} else if(_ch->is_chorus_auto()) {
			set_control_value(chorus_preset, "auto");
		} else {
			set_control_value(chorus_preset, MixerChannel::chorus_preset_to_config(_ch->chorus().preset).name);
		}
	}
}

void MixerControl::set_volume(int _id, float _value)
{
	if(_id >= MixerChannel::CategoryCount) {
		auto channel = m_channels[_id].ch.get();
		assert(channel);
		channel->set_volume_master(_value);
	} else if(_id < 0) {
		m_mixer->set_volume_master(_value);
	} else {
		m_mixer->set_volume_cat(static_cast<MixerChannel::Category>(_id), _value);
	}
	set_control_value(get_element(str_format("ch_vol_progress_%d", _id)), _value * 100.0);
}

void MixerControl::set_volume_slider(int _id, float _master)
{
	set_volume_slider(
		get_element(str_format("ch_vol_%d", _id)),
		get_element(str_format("ch_vol_progress_%d", _id)),
		_id, _master
	);
}

void MixerControl::set_volume_slider(int _id, float _master, float _left, float _right)
{
	int slider_value = 130 - _master * 100;
	auto slider = get_element(str_format("ch_vol_%d", _id));
	set_control_value(slider, slider_value);
	set_volume_label(_id, _left, _right);
}

void MixerControl::set_volume_slider(Rml::Element *_slider, Rml::Element *_progress, int _id, float _value)
{
	int slider_value = 130 - _value * 100;
	set_control_value(_slider, slider_value);
	set_control_value(_slider, _value, "real_value");
	set_control_value(_progress, _value * 100);
	set_volume_label(_id, _value);
}

void MixerControl::set_volume_label(int _id, float _master)
{
	auto value_label = get_element(str_format("ch_vol_value_%d", _id));
	value_label->SetInnerRML(str_format("%.2f", _master));
}

void MixerControl::set_volume_label(int _id, float _left, float _right)
{
	auto value_label = get_element(str_format("ch_vol_value_%d", _id));
	value_label->SetInnerRML(str_format("L:%.2f R:%.2f", _left, _right));
}

bool MixerControl::on_volume_change(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	double value = str_parse_real_num(val);
	value = (130.0 - value) / 100.0;
	set_volume(_chid, value);
	set_volume_label(_chid, value);
	return true;
}

bool MixerControl::on_volume_auto(Rml::Event &_evt, int _chid)
{
	auto tgt = get_button_element(_evt);
	bool autovol = !is_active(tgt);
	auto channel = m_channels[_chid].ch.get();
	assert(channel);
	auto slider = get_element(str_format("ch_vol_%d", _chid));
	set_disabled(slider, autovol);
	channel->set_volume_auto(autovol);
	set_active(tgt, autovol);
	if(!autovol) {
		float value = slider->GetAttribute("value", 1.0);
		value = (130.0 - value) / 100.0;
		set_volume(_chid, value);
		set_volume_label(_chid, value);
	}
	if(channel->features() & MixerChannel::Feature::HasAutoFilter) {
		auto check = get_element(str_format("ch_filter_en_%d", _chid));
		set_disabled(check, autovol);
	}
	return true;
}

void MixerControl::set_balance(int _id, float _value, bool _update_links)
{
	if(_id >= MixerChannel::CategoryCount) {
		auto channel = m_channels[_id].ch.get();
		assert(channel);
		channel->set_balance(_value);
		auto cfg = channel->config_map().find(MixerChannel::ConfigParameter::Balance);
		if(_update_links && cfg != channel->config_map().end()) {
			auto links = m_ch_links.find(cfg->second);
			if(links == m_ch_links.end()) {
				PDEBUGF(LOG_V0, LOG_MIXER, "Links for balance not defined\n");
				return;
			}
			for(auto & linked_ch : links->second) {
				if(linked_ch->id() != _id) {
					set_balance(linked_ch->id(), _value, false);
					set_balance_slider(linked_ch->id(), _value);
				}
			}
		}
	} else if(_id < 0) {
		// master
	} else {
		// category
	}

	auto progress = get_element(str_format("ch_bal_progress_%s_%d", _value < .0 ? "l" : "r", _id));
	set_control_value(progress, std::abs(_value));
	progress = get_element(str_format("ch_bal_progress_%s_%d", _value >= .0 ? "l" : "r", _id));
	set_control_value(progress, 0.0);
}

void MixerControl::set_balance_slider(int _id, float _value)
{
	int slider_value = (_value * 100.0) / 2.0 + 50.0; // 0..100
	auto slider = get_element(str_format("ch_bal_%d", _id));
	set_control_value(slider, slider_value);

	auto progress = get_element(str_format("ch_bal_progress_%s_%d", _value < .0 ? "l" : "r", _id));
	set_control_value(progress, std::abs(_value));
	progress = get_element(str_format("ch_bal_progress_%s_%d", _value >= .0 ? "l" : "r", _id));
	set_control_value(progress, 0.0);

	set_balance_label(_id, _value);
}

void MixerControl::set_balance_label(int _id, float _value)
{
	auto value_label = get_element(str_format("ch_bal_value_%d", _id));
	value_label->SetInnerRML(str_format("%.2f", _value));
}

bool MixerControl::on_balance_change(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	double value = str_parse_real_num(val);
	value = (value / 100.0) * 2.0 - 1.0; // -1..+1
	set_balance(_chid, value, true);
	set_balance_label(_chid, value);
	return true;
}

void MixerControl::set_mute(int _chid, bool _muted)
{
	if(_chid >= MixerChannel::CategoryCount) {
		auto channel = m_channels[_chid].ch.get();
		assert(channel);
		channel->set_muted(_muted);
		if(_muted) {
			set_active(get_element(str_format("ch_solo_%d", _chid)), false);
		}
	} else if(_chid < 0) {
		m_mixer->set_muted(_muted);
	} else {
		m_mixer->set_muted_cat(static_cast<MixerChannel::Category>(_chid), _muted);
	}
	set_active(get_element(str_format("ch_mute_%d", _chid)), _muted);
}

void MixerControl::set_solo(int _chid, bool _soloed)
{
	assert(_chid >= MixerChannel::CategoryCount);

	auto channel = m_channels[_chid].ch.get();
	assert(channel);
	if(_soloed) {
		set_mute(_chid, false);
	}
	for(auto & ch : m_channels) {
		if(ch.first != _chid) {
			set_mute(ch.first, _soloed);
		}
	}
	set_active(get_element(str_format("ch_solo_%d", _chid)), _soloed);
}

bool MixerControl::on_mute(Rml::Event &_evt, int _chid)
{
	auto tgt = get_button_element(_evt);
	bool muted = !is_active(tgt);
	set_mute(_chid, muted);
	return true;
}

bool MixerControl::on_solo(Rml::Event &_evt, int _chid)
{
	auto tgt = get_button_element(_evt);
	bool soloed = !is_active(tgt);
	set_solo(_chid, soloed);
	return true;
}

bool MixerControl::on_setting(Rml::Event &_evt, int _chid)
{
	auto tgt = get_button_element(_evt);
	auto setting_panel = get_element(str_format("ch_setting_%d", _chid));
	auto sliders_panel = get_element(str_format("ch_sliders_%d", _chid));
	if(setting_panel->IsClassSet("d-none")) {
		set_active(tgt, true);
		setting_panel->SetClass("d-none", false);
		sliders_panel->SetClass("d-none", true);
	} else {
		set_active(tgt, false);
		setting_panel->SetClass("d-none", true);
		sliders_panel->SetClass("d-none", false);
	}
	return true;
}

void MixerControl::set_filter(int _chid, std::string _preset)
{
	bool was_auto = m_channels[_chid].ch->is_filter_auto();
	auto custom_container = get_element(str_format("ch_filter_custom_%d", _chid));
	if(was_auto && _preset != "auto") {
		m_channels[_chid].ch->set_filter_auto(false);
	}
	if(_preset == "custom") {
		custom_container->SetClass("d-none", false);
		m_channels[_chid].ch->set_filter("custom");
		update_filter_chain(_chid);
	} else {
		custom_container->SetClass("d-none", true);
		if(_preset == "auto") {
			m_channels[_chid].ch->set_filter_auto(true);
		} else {
			m_channels[_chid].ch->set_filter(_preset);
		}
	}
}

void MixerControl::add_filter(int _chid, const Dsp::Filter *_filter, size_t _filter_idx)
{
	auto chain_container = get_element(str_format("ch_filter_chain_%d", _chid));

	Rml::ElementPtr filter_container = m_wnd->CreateElement("div");
		filter_container->SetId(str_format("filter_dsp_%d_%d", _filter_idx, _chid));
		filter_container->SetAttribute("index", _filter_idx);
		filter_container->SetClassNames("filter_dsp");

	Rml::ElementPtr dsp_kind = m_wnd->CreateElement("div");
		dsp_kind->SetClassNames("filter_dsp_kind");

	Rml::ElementPtr kind = m_wnd->CreateElement("select");
		kind->SetId(str_format("filter_dsp_kind_%d_%d", _filter_idx, _chid));
		kind->SetClassNames("romshell");
		auto select = dynamic_cast<Rml::ElementFormControlSelect*>(kind.get());
		select->Add("Low Pass", "LowPass");
		select->Add("High Pass", "HighPass");
		select->Add("Band Pass", "BandPass");
		select->Add("Band Stop", "BandStop");
		select->Add("Low Shelf", "LowShelf");
		select->Add("High Shelf", "HighShelf");
		select->Add("Band Shelf", "BandShelf");
		auto name = _filter->getName();
		str_replace_all(name, " ", "");
		set_control_value(kind.get(), name);

	Rml::ElementPtr remove = m_wnd->CreateElement("button");
		remove->SetClassNames("filter_dsp_remove romshell");
		remove->SetId(str_format("filter_dsp_remove_%d_%d", _filter_idx, _chid));
		remove->SetInnerRML("<btnicon /><span></span>");

	register_target_cb(chain_container, kind->GetId(), "change",
			//[=](Rml::Event &_evt) -> bool { return on_filter_change(_evt, _chid, _filter_idx); }
			std::bind(&MixerControl::on_filter_change, this, _1, _chid, _filter_idx)
	);
	register_target_cb(chain_container, remove->GetId(), "click",
			std::bind(&MixerControl::on_filter_remove, this, _1, _chid, _filter_idx));

	dsp_kind->AppendChild(std::move(kind));
	dsp_kind->AppendChild(std::move(remove));
	filter_container->AppendChild(move(dsp_kind));

	for(auto paramID : _filter->getParamIDs()) {
		if(paramID == Dsp::idSampleRate) {
			continue;
		}
		auto param_info = Dsp::ParamInfo::defaults(paramID);
		auto id = str_format("filter_dsp_%s_%d_%d", param_info.getSlug(), _filter_idx, _chid);
		Rml::ElementPtr parameter = m_wnd->CreateElement("div");
			parameter->SetId(id);
			parameter->SetClassNames("filter_dsp_parameter");
			parameter->SetInnerRML(str_format("<div class=\"ch_label\">%s</div>", param_info.getName()));
			parameter->AppendChild(
				create_spinner(id.c_str(), paramID, _filter->getParam(paramID), _chid, _filter_idx)
			);
		filter_container->AppendChild(std::move(parameter));
	}

	chain_container->AppendChild(std::move(filter_container));
}

void MixerControl::update_filter_chain(int _chid)
{
	register_lazy_update_fn([=](){
		auto filters = get_element(str_format("ch_filter_chain_%d", _chid));
		unregister_target_cb(filters);
		filters->SetInnerRML("");

		auto & chain = m_channels[_chid].ch->filter_chain();
		for(size_t i = 0; i < chain.size(); i++) {
			add_filter(_chid, chain[i].get(), i);
		}
	});
}

bool MixerControl::on_filter_preset(Rml::Event &_evt, int _chid)
{
	std::string preset = _evt.GetParameter("value", std::string());
	set_filter(_chid, preset);
	return true;
}

bool MixerControl::on_filter_change(Rml::Event &_event, int _chid, size_t _filter_idx)
{
	std::string new_kind = _event.GetParameter("value", std::string());
	m_channels[_chid].ch->set_filter_kind(_filter_idx, new_kind);
	update_filter_chain(_chid);
	return true;
}

bool MixerControl::on_filter_add(Rml::Event &, int _chid)
{
	size_t index = m_channels[_chid].ch->add_filter("lowpass");
	add_filter(_chid, m_channels[_chid].ch->filter_chain()[index].get(), index);
	return true;
}

bool MixerControl::on_filter_remove(Rml::Event &, int _chid, size_t _filter_idx)
{
	m_channels[_chid].ch->remove_filter(_filter_idx);
	update_filter_chain(_chid);
	return true;
}

bool MixerControl::on_filter_enable(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	m_channels[_chid].ch->enable_filter(val == "on");
	return false;
}

bool MixerControl::on_filter_setting(Rml::Event &_evt, int _chid)
{
	auto tgt = get_button_element(_evt);
	auto setting_panel = get_element(str_format("ch_setting_%d", _chid));
	auto fsetting_panel = get_element(str_format("ch_filter_container_%d", _chid));
	if(fsetting_panel->IsClassSet("d-none")) {
		set_active(tgt, true);
		setting_panel->SetClass("filter_setting_active", true);
		fsetting_panel->SetClass("d-none", false);
	} else {
		set_active(tgt, false);
		setting_panel->SetClass("filter_setting_active", false);
		fsetting_panel->SetClass("d-none", true);
	}
	return true;
}

void MixerControl::incdec_filter_param(Rml::Element *_spinner, Dsp::ParamID _param_id, int _chid, int _dspid, bool _increase)
{
	double value = m_channels[_chid].ch->get_filter_param(_dspid, _param_id);
	double old_value = value;
	double amount = 1.0;
	if(_param_id == Dsp::idBandwidthHz || _param_id == Dsp::idFrequency) {
		amount = 50.0;
	}
	if(_increase) {
		value += amount;
	} else {
		value -= amount;
	}
	value = std::min(Dsp::ParamInfo::defaults(_param_id).getMax(), value);
	value = std::max(Dsp::ParamInfo::defaults(_param_id).getMin(), value);
	if(value != old_value) {
		m_channels[_chid].ch->set_filter_param(_dspid, _param_id, value);
		set_spinner_value(_spinner, value);
	}
}

Rml::ElementPtr MixerControl::create_spinner(std::string _param_name, Dsp::ParamID _param_id, double _value, int _chid, int _dspid)
{
	auto container = get_element(str_format("ch_filter_chain_%d", _chid));

	Rml::ElementPtr spinner =  m_wnd->CreateElement("div");
	spinner->SetId(str_format("%s_spinner", _param_name.c_str()));
	spinner->SetClassNames("spinner");

	Rml::ElementPtr dec = m_wnd->CreateElement("button");
		dec->SetClassNames("decrease romshell");
		dec->SetId(str_format("%s_dec", _param_name.c_str()));
		dec->SetInnerRML("<span>-</span>");

	Rml::ElementPtr val = m_wnd->CreateElement("input");
		val->SetAttribute("type", "text");
		set_disabled(val.get(), true);
		val->SetClassNames("value");
		val->SetId(str_format("%s_val", _param_name.c_str()));

	Rml::ElementPtr inc = m_wnd->CreateElement("button");
		inc->SetClassNames("increase romshell");
		inc->SetId(str_format("%s_inc", _param_name.c_str()));
		inc->SetInnerRML("<span>+</span>");

	register_target_cb(container, inc->GetId(), "mousedown",
		std::bind(&MixerControl::on_spinner_btn, this, _1, spinner.get(), _param_id, _chid, _dspid, true));
	register_target_cb(container, dec->GetId(), "mousedown",
		std::bind(&MixerControl::on_spinner_btn, this, _1, spinner.get(), _param_id, _chid, _dspid, false));
	register_target_cb(container, inc->GetId(), "click",
		std::bind(&MixerControl::on_spinner_btn, this, _1, spinner.get(), _param_id, _chid, _dspid, true));
	register_target_cb(container, dec->GetId(), "click",
		std::bind(&MixerControl::on_spinner_btn, this, _1, spinner.get(), _param_id, _chid, _dspid, false));
	register_target_cb(container, inc->GetId(), "keydown",
		std::bind(&MixerControl::on_spinner_btn, this, _1, spinner.get(), _param_id, _chid, _dspid, true));
	register_target_cb(container, dec->GetId(), "keydown",
		std::bind(&MixerControl::on_spinner_btn, this, _1, spinner.get(), _param_id, _chid, _dspid, false));

	spinner->AppendChild(std::move(dec));
	spinner->AppendChild(std::move(val));
	spinner->AppendChild(std::move(inc));

	set_spinner_value(spinner.get(), _value);

	return spinner;
}

bool MixerControl::on_spinner_btn(Rml::Event &_evt, Rml::Element *_spinner, Dsp::ParamID _param_id, int _chid, int _dspid, bool _increase)
{
	if(_evt.GetId() == Rml::EventId::Mousedown) {
		m_click_cb = std::bind(&MixerControl::incdec_filter_param, this, _spinner, _param_id, _chid, _dspid, _increase);
		m_click_cb();
		m_gui->timers().activate_timer(m_click_timer, 500_ms, 50_ms, true);
	} else if(_evt.GetId() == Rml::EventId::Click) {
		m_gui->timers().deactivate_timer(m_click_timer);
		m_click_cb = nullptr;
	} else {
		auto key = get_key_identifier(_evt);
		if(key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
			incdec_filter_param(_spinner, _param_id, _chid, _dspid, _increase);
			return true;
		}
		Window::on_keydown(_evt);
		return false;
	}
	return true;
}

void MixerControl::set_spinner_value(Rml::Element *_spinner, double _value)
{
	Rml::ElementList val_el;
	_spinner->GetElementsByTagName(val_el, "input");
	assert(!val_el.empty());
	val_el[0]->SetInnerRML(str_format("%g", _value));
}

bool MixerControl::on_reverb_preset(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	if(_chid >= MixerChannel::CategoryCount) {
		auto channel = m_channels[_chid].ch.get();
		assert(channel);
		if(val == "auto") {
			channel->set_reverb_auto(true);
			channel->enable_reverb(true);
		} else {
			channel->set_reverb_auto(false);
			channel->set_reverb(val, true);
		}
	} else if(_chid >= 0) {
		m_mixer->set_reverb(static_cast<MixerChannel::Category>(_chid), val);
	}
	return true;
}

bool MixerControl::on_chorus_preset(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	auto channel = m_channels[_chid].ch.get();
	assert(channel);
	if(val == "auto") {
		channel->set_chorus_auto(true);
		channel->enable_chorus(true);
	} else {
		channel->set_chorus_auto(false);
		channel->set_chorus(val, true);
	}
	return true;
}

bool MixerControl::on_crossfeed_preset(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	auto channel = m_channels[_chid].ch.get();
	assert(channel);
	if(val == "auto") {
		channel->set_crossfeed_auto(true);
		channel->enable_crossfeed(true);
	} else {
		channel->set_crossfeed_auto(false);
		channel->set_crossfeed(val, true);
	}
	return true;
}

bool MixerControl::on_resampling_mode(Rml::Event &_evt, int _chid)
{
	std::string val = _evt.GetParameter("value", std::string());
	auto channel = m_channels[_chid].ch.get();
	assert(channel);
	if(val == "auto") {
		channel->set_resampling_auto(true);
	} else {
		channel->set_resampling_auto(false);
		channel->set_resampling_type(val);
	}
	return true;
}

Rml::ElementPtr MixerControl::create_master_block()
{
	Rml::ElementPtr ch_block = m_wnd->CreateElement("div");
		ch_block->SetClassNames("ch_block");
		ch_block->SetId("ch_master");

	Rml::ElementPtr ch_sliders_container = m_wnd->CreateElement("div");
		ch_sliders_container->SetClassNames("ch_sliders_container");
		ch_sliders_container->AppendChild(create_volume_slider(-1));

	ch_block->AppendChild(std::move(ch_sliders_container));

	ch_block->AppendChild(create_AMS_buttons(-1, false, true, false));

	Rml::ElementPtr ch_name = m_wnd->CreateElement("div");
		ch_name->SetClassNames("ch_name");
		ch_name->SetInnerRML("Master");

	ch_block->AppendChild(move(ch_name));

	return ch_block;
}

Rml::ElementPtr MixerControl::create_category_block(MixerChannel::Category _id)
{
	Rml::ElementPtr ch_block = m_wnd->CreateElement("div");
		ch_block->SetClassNames("ch_block");
		ch_block->SetId(str_format("ch_%d", _id));

	if(_id == MixerChannel::SOUNDFX) {
		Rml::ElementPtr ch_setting_container = m_wnd->CreateElement("div");
			ch_setting_container->SetClassNames("ch_setting_container d-none");
			ch_setting_container->SetId(str_format("ch_setting_%d", _id));
			ch_setting_container->AppendChild(create_reverb_setting(MixerChannel::SOUNDFX, false));

		Rml::ElementPtr setting_btn = m_wnd->CreateElement("button");
			setting_btn->SetClassNames("ch_setting_btn romshell");
			setting_btn->SetId(str_format("ch_setting_btn_%d", MixerChannel::SOUNDFX));
			setting_btn->SetInnerRML("<span>Setting</span>");

		register_target_cb(setting_btn.get(), "click", std::bind(&MixerControl::on_setting, this, _1, _id));

		ch_block->AppendChild(std::move(setting_btn));
		ch_block->AppendChild(std::move(ch_setting_container));
	}

	Rml::ElementPtr ch_sliders_container = m_wnd->CreateElement("div");
		ch_sliders_container->SetClassNames("ch_sliders_container");
		ch_sliders_container->SetId(str_format("ch_sliders_%d", _id));
		ch_sliders_container->AppendChild(create_volume_slider(_id));

	ch_block->AppendChild(std::move(ch_sliders_container));

	ch_block->AppendChild(create_AMS_buttons(_id, false, true, false));

	Rml::ElementPtr ch_name = m_wnd->CreateElement("div");
		ch_name->SetId(str_format("ch_name_%d", _id));
		ch_name->SetClassNames("ch_name");
		if(_id == MixerChannel::AUDIOCARD) {
			ch_name->SetInnerRML("Audio cards");
		} else {
			ch_name->SetInnerRML("Sound FX");
		}

	ch_block->AppendChild(move(ch_name));

	return ch_block;
}

Rml::ElementPtr MixerControl::create_channel_block(const MixerChannel *_ch)
{
	Rml::ElementPtr ch_block_container = m_wnd->CreateElement("div");
		ch_block_container->SetId(str_format("ch_block_container_%d", _ch->id()));
		ch_block_container->SetClassNames("ch_block_container");

	Rml::ElementPtr ch_block = m_wnd->CreateElement("div");
		ch_block->SetClassNames("ch_block");
		ch_block->SetId(str_format("ch_%d", _ch->id()));

	Rml::ElementPtr ch_setting_container = m_wnd->CreateElement("div");
		ch_setting_container->SetClassNames("ch_setting_container d-none");
		ch_setting_container->SetId(str_format("ch_setting_%d", _ch->id()));

	int f = _ch->features();
	if(f & MixerChannel::HasFilter) {
		ch_setting_container->AppendChild(create_filters_setting(_ch->id(), f & MixerChannel::HasAutoFilter));
	}
	if(f & MixerChannel::HasReverb) {
		ch_setting_container->AppendChild(create_reverb_setting(_ch->id(), f & MixerChannel::HasAutoReverb));
	}
	if(f & MixerChannel::HasChorus) {
		ch_setting_container->AppendChild(create_chorus_setting(_ch->id(), f & MixerChannel::HasAutoChorus));
	}
	if(f & MixerChannel::HasCrossfeed) {
		ch_setting_container->AppendChild(create_crossfeed_setting(_ch->id()));
	}
	if(f & MixerChannel::HasResamplingType) {
		ch_setting_container->AppendChild(create_resampling_setting(_ch->id(), f & MixerChannel::HasAutoResamplingType));
	}

	if(ch_setting_container->GetFirstChild()) {
		Rml::ElementPtr setting_btn = m_wnd->CreateElement("button");
			setting_btn->SetClassNames("ch_setting_btn romshell");
			setting_btn->SetId(str_format("ch_setting_btn_%d", _ch->id()));
			setting_btn->SetInnerRML("<span>Setting</span>");

		register_target_cb(setting_btn.get(), "click", std::bind(&MixerControl::on_setting, this, _1, _ch->id()));

		ch_block->AppendChild(std::move(setting_btn));
		ch_block->AppendChild(std::move(ch_setting_container));
	}

	Rml::ElementPtr ch_sliders_container = m_wnd->CreateElement("div");
		ch_sliders_container->SetClassNames("ch_sliders_container");
		ch_sliders_container->SetId(str_format("ch_sliders_%d", _ch->id()));
		if(f & MixerChannel::HasBalance) {
			ch_sliders_container->AppendChild(create_balance_slider(_ch->id()));
		}

	ch_sliders_container->AppendChild(create_volume_slider(_ch->id()));

	ch_block->AppendChild(std::move(ch_sliders_container));

	ch_block->AppendChild(create_AMS_buttons(_ch->id(), f & MixerChannel::HasAutoVolume, true, true));

	Rml::ElementPtr ch_name = m_wnd->CreateElement("div");
		ch_name->SetId(str_format("ch_name_%d", _ch->id()));
		ch_name->SetClassNames("ch_name");
		ch_name->SetInnerRML(_ch->name());

	ch_block->AppendChild(move(ch_name));

	ch_block_container->AppendChild(move(ch_block));

	return ch_block_container;
}

Rml::ElementPtr MixerControl::create_AMS_buttons(int _id, bool _auto, bool _mute, bool _solo)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
		container->SetClassNames("ch_AMS");

	if(_auto) {
		Rml::ElementPtr setting = m_wnd->CreateElement("button");
			setting->SetClassNames("ch_volume_auto romshell");
			setting->SetId(str_format("ch_volume_auto_%d", _id));
			setting->SetInnerRML("<span>A</span>");

		register_target_cb(setting.get(), setting->GetId(), "click",
				std::bind(&MixerControl::on_volume_auto, this, _1, _id));

		container->AppendChild(std::move(setting));
	}
	if(_mute) {
		Rml::ElementPtr mute = m_wnd->CreateElement("button");
			mute->SetId(str_format("ch_mute_%d", _id));
			mute->SetClassNames("ch_mute romshell");
			mute->SetInnerRML("<span>M</span>");

		register_target_cb(mute.get(), "click", std::bind(&MixerControl::on_mute, this, _1, _id));

		container->AppendChild(std::move(mute));
	}

	if(_solo) {
		Rml::ElementPtr solo = m_wnd->CreateElement("button");
			solo->SetId(str_format("ch_solo_%d", _id));
			solo->SetClassNames("ch_solo romshell");
			solo->SetInnerRML("<span>S</span>");

		register_target_cb(solo.get(), "click", std::bind(&MixerControl::on_solo, this, _1, _id));

		container->AppendChild(std::move(solo));
	}

	return container;
}

Rml::ElementPtr MixerControl::create_volume_slider(int _id)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
	container->SetClassNames("ch_volume_container");

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Volume");

	container->AppendChild(std::move(label));

	Rml::ElementPtr value = m_wnd->CreateElement("div");
		value->SetId(str_format("ch_vol_value_%d", _id));
		value->SetClassNames("ch_volume_value ch_label");
		value->SetInnerRML("0.0");

	container->AppendChild(std::move(value));

	Rml::ElementPtr slider_container = m_wnd->CreateElement("div");
		slider_container->SetClassNames("ch_volume_slider_container");

	Rml::ElementPtr slider = m_wnd->CreateElement("input");
		slider->SetId(str_format("ch_vol_%d", _id));
		slider->SetClassNames("ch_volume_slider");
		slider->SetAttribute("type", "range");
		slider->SetAttribute("min", "0");
		slider->SetAttribute("max", "130");
		slider->SetAttribute("step", "1");
		slider->SetAttribute("orientation", "vertical");
		slider->SetAttribute("value", "100");

	Rml::ElementPtr progress = m_wnd->CreateElement("progress");
		progress->SetId(str_format("ch_vol_progress_%d", _id));
		progress->SetClassNames("ch_volume_progress");
		progress->SetAttribute("direction", "top");
		progress->SetAttribute("max", "130");
		progress->SetAttribute("value", "100");

	Rml::ElementPtr notch = m_wnd->CreateElement("div");
		notch->SetClassNames("ch_volume_notch");

	register_target_cb(slider.get(), "change", std::bind(&MixerControl::on_volume_change, this, _1, _id));

	slider_container->AppendChild(std::move(notch));
	slider_container->AppendChild(std::move(progress));
	slider_container->AppendChild(std::move(slider));
	container->AppendChild(std::move(slider_container));

	return container;
}

Rml::ElementPtr MixerControl::create_balance_slider(int _id)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
		container->SetClassNames("ch_balance_container");

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Balance");

	Rml::ElementPtr value = m_wnd->CreateElement("div");
		value->SetId(str_format("ch_bal_value_%d", _id));
		value->SetClassNames("ch_balance_value ch_label");
		value->SetInnerRML("0.0");

	Rml::ElementPtr slider = m_wnd->CreateElement("input");
		slider->SetId(str_format("ch_bal_%d", _id));
		slider->SetClassNames("ch_balance_slider");
		slider->SetAttribute("type", "range");
		slider->SetAttribute("min", "0");
		slider->SetAttribute("max", "100");
		slider->SetAttribute("step", "1");
		slider->SetAttribute("value", "50");

	Rml::ElementPtr progress_l = m_wnd->CreateElement("progress");
		progress_l->SetId(str_format("ch_bal_progress_l_%d", _id));
		progress_l->SetClassNames("ch_balance_progress ch_balance_progress_left");
		progress_l->SetAttribute("direction", "left");
		progress_l->SetAttribute("max", "1.0");
		progress_l->SetAttribute("value", "0.0");

	Rml::ElementPtr progress_r = m_wnd->CreateElement("progress");
		progress_r->SetId(str_format("ch_bal_progress_r_%d", _id));
		progress_r->SetClassNames("ch_balance_progress ch_balance_progress_right");
		progress_r->SetAttribute("direction", "right");
		progress_r->SetAttribute("max", "1.0");
		progress_r->SetAttribute("value", "0.0");
			
	Rml::ElementPtr notch = m_wnd->CreateElement("div");
		notch->SetClassNames("ch_balance_notch");

	register_target_cb(slider.get(), "change", std::bind(&MixerControl::on_balance_change, this, _1, _id));

	container->AppendChild(std::move(label));
	container->AppendChild(std::move(value));
	container->AppendChild(std::move(notch));
	container->AppendChild(std::move(progress_l));
	container->AppendChild(std::move(progress_r));
	container->AppendChild(std::move(slider));

	return container;
}

Rml::ElementPtr MixerControl::create_filters_setting(int _chid, bool _has_auto)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
	container->SetClassNames("ch_subsetting_container ch_filter");
	container->SetId(str_format("ch_filter_%d", _chid));
	
	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Filters");

	Rml::ElementPtr enable = m_wnd->CreateElement("input");
		enable->SetId(str_format("ch_filter_en_%d", _chid));
		enable->SetClassNames("ch_feature_enable romshell");
		enable->SetAttribute("type", "checkbox");
		enable->SetInnerRML("<span>enable</span>");

	Rml::ElementPtr setting = m_wnd->CreateElement("button");
		setting->SetClassNames("ch_setting romshell");
		setting->SetId(str_format("ch_filter_setting_%d", _chid));
		setting->SetInnerRML("<btnicon /><span></span>");

	register_target_cb(enable.get(), "change", std::bind(&MixerControl::on_filter_enable, this, _1, _chid));
	register_target_cb(setting.get(), "click", std::bind(&MixerControl::on_filter_setting, this, _1, _chid));

	container->AppendChild(std::move(label));
	container->AppendChild(std::move(enable));
	container->AppendChild(std::move(setting));

	container->AppendChild(create_filters_container(_chid, _has_auto));

	return container;
}

Rml::ElementPtr MixerControl::create_filters_container(int _chid, bool _has_auto)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
		container->SetClassNames("ch_filter_container d-none");
		container->SetId(str_format("ch_filter_container_%d", _chid));

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Preset");

	Rml::ElementPtr preset = m_wnd->CreateElement("select");
		preset->SetId(str_format("ch_filter_preset_%d", _chid));
		preset->SetClassNames("ch_filter_preset romshell");
		auto select = dynamic_cast<Rml::ElementFormControlSelect*>(preset.get());
		select->Add("none",  "none");
		if(_has_auto) {
			select->Add("auto", "auto");
		}
		select->Add("pc-speaker", "pc-speaker");
		select->Add("custom", "custom");

	Rml::ElementPtr custom = m_wnd->CreateElement("div");
		custom->SetId(str_format("ch_filter_custom_%d", _chid));
		custom->SetClassNames("ch_filter_custom d-none");

	Rml::ElementPtr chain = m_wnd->CreateElement("div");
		chain->SetId(str_format("ch_filter_chain_%d", _chid));
		chain->SetClassNames("ch_filter_chain");

	Rml::ElementPtr add = m_wnd->CreateElement("button");
		add->SetClassNames("ch_add_filter romshell");
		add->SetId(str_format("ch_add_filter_%d", _chid));
		add->SetInnerRML("<span>+</span>");

	register_target_cb(preset.get(), "change", std::bind(&MixerControl::on_filter_preset, this, _1, _chid));
	register_target_cb(add.get(), "click", std::bind(&MixerControl::on_filter_add, this, _1, _chid));

	custom->AppendChild(std::move(chain));
	custom->AppendChild(std::move(add));

	container->AppendChild(std::move(label));
	container->AppendChild(std::move(preset));
	container->AppendChild(std::move(custom));

	return container;
}

Rml::ElementPtr MixerControl::create_reverb_setting(int _id, bool _has_auto)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
	container->SetClassNames("ch_subsetting_container ch_reverb");

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Reverb");
	container->AppendChild(std::move(label));

	Rml::ElementPtr preset = m_wnd->CreateElement("select");
		preset->SetId(str_format("ch_reverb_preset_%d", _id));
		preset->SetClassNames("ch_reverb_preset romshell");
		auto select = dynamic_cast<Rml::ElementFormControlSelect*>(preset.get());
		select->Add("none", "none");
		if(_has_auto) {
			select->Add("auto", "auto");
		}
		select->Add("tiny", "tiny");
		select->Add("small", "small");
		select->Add("medium","medium");
		select->Add("large", "large");
		select->Add("huge", "huge");

	register_target_cb(preset.get(), "change", std::bind(&MixerControl::on_reverb_preset, this, _1, _id));

	container->AppendChild(move(preset));

	return container;
}

Rml::ElementPtr MixerControl::create_chorus_setting(int _id, bool _has_auto)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
	container->SetClassNames("ch_subsetting_container ch_chorus");

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Chorus");
	container->AppendChild(std::move(label));

	Rml::ElementPtr preset = m_wnd->CreateElement("select");
		preset->SetId(str_format("ch_chorus_preset_%d", _id));
		preset->SetClassNames("ch_chorus_preset romshell");
		auto select = dynamic_cast<Rml::ElementFormControlSelect*>(preset.get());
		select->Add("none", "none");
		if(_has_auto) {
			select->Add("auto", "auto");
		}
		select->Add("light", "light");
		select->Add("normal", "normal");
		select->Add("strong", "strong");
		select->Add("heavy", "heavy");

	register_target_cb(preset.get(), "change", std::bind(&MixerControl::on_chorus_preset, this, _1, _id));

	container->AppendChild(move(preset));

	return container;
}

Rml::ElementPtr MixerControl::create_crossfeed_setting(int _id)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
	container->SetClassNames("ch_subsetting_container ch_crossfeed");

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Crossfeed");
	container->AppendChild(std::move(label));

	Rml::ElementPtr preset = m_wnd->CreateElement("select");
		preset->SetId(str_format("ch_crossfeed_preset_%d", _id));
		preset->SetClassNames("ch_crossfeed_preset romshell");
		auto select = dynamic_cast<Rml::ElementFormControlSelect*>(preset.get());
		select->Add("none",  "none");
		select->Add("bauer", "bauer");
		select->Add("meier", "meier");
		select->Add("moy",   "moy");

	register_target_cb(preset.get(), "change", std::bind(&MixerControl::on_crossfeed_preset, this, _1, _id));

	container->AppendChild(move(preset));

	return container;
}

Rml::ElementPtr MixerControl::create_resampling_setting(int _id, bool _has_auto)
{
	Rml::ElementPtr container = m_wnd->CreateElement("div");
	container->SetClassNames("ch_subsetting_container ch_resampling");

	Rml::ElementPtr label = m_wnd->CreateElement("div");
		label->SetClassNames("ch_label");
		label->SetInnerRML("Resampling mode");
	container->AppendChild(std::move(label));

	Rml::ElementPtr preset = m_wnd->CreateElement("select");
		preset->SetId(str_format("ch_resampling_mode_%d", _id));
		preset->SetClassNames("ch_resampling_mode romshell");
		auto select = dynamic_cast<Rml::ElementFormControlSelect*>(preset.get());
		if(_has_auto) {
			select->Add("auto", "auto");
		}
		select->Add("sinc", "sinc");
		select->Add("linear", "linear");
		select->Add("hold", "hold");

	register_target_cb(preset.get(), "change",
		std::bind(&MixerControl::on_resampling_mode, this, _1, _id));

	container->AppendChild(move(preset));

	return container;
}

void MixerControl::on_slider_dragstart(Rml::Event &)
{
	m_is_sliding = true;
}

void MixerControl::on_slider_dragend(Rml::Event &)
{
	m_is_sliding = false;
}