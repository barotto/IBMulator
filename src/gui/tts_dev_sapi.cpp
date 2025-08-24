/*
 * Copyright (C) 2025  Marco Bortolin
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

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sphelper.h>
#include <sapi.h>
#include <cmath>

#include "utils.h"
#include "wincompat.h"
#include "tts_dev.h"
#include "tts_dev_sapi.h"
#include "tts_format_msxml.h"

void TTSDev_SAPI::open(const std::vector<std::string> &_params)
{
	PINFOF(LOG_V0, LOG_GUI, "TTS: Initializing the SAPI device.\n");

	if(_params.size() < 2) {
		throw std::logic_error("invalid number of parameters");
	}

	// CoInitializeEx must be called at least once, and is usually called only once,
	// for each thread that uses the COM library. Multiple calls to CoInitializeEx
	// by the same thread are allowed as long as they pass the same concurrency
	// flag, but subsequent valid calls return S_FALSE.
	// SDL chould call CoInitialize, so the following might be superfluous and return S_FALSE
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_voice);
	if(FAILED(hr) || !m_voice) {
		m_voice = nullptr;
		CoUninitialize();
		throw std::runtime_error("cannot create the voice");
	}

	m_format = std::make_unique<TTSFormat_MSXML>(_params[1]);


	if(_params[0].empty() || _params[0] == "default" || _params[0] == "auto") {
		PINFOF(LOG_V0, LOG_GUI, "%s: Using the default system voice.\n", name());
		return;
	}
	int voice_num = -1;
	try {
		// try as a voice number first
		voice_num = str_parse_int_num(_params[0]);
	} catch(std::runtime_error &e) {
		// it's not a number
	}
	if(voice_num >= 0) {
		// use the parameter as a voice number
		if(voice_num <= 1) {
			// it's the default system voice
			PINFOF(LOG_V0, LOG_GUI, "%s: Using the default system voice.\n", name());
			return;
		}
		try {
			set_voice(voice_num);
		} catch(std::invalid_argument &e) {
			PWARNF(LOG_V0, LOG_GUI, "%s: %s.\n", name(), e.what());
			PINFOF(LOG_V0, LOG_GUI, "%s: Using the default system voice.\n", name());
			display_voices();
		} catch(std::runtime_error &e) {
			// don't release the voice, let the system use it if available
			PERRF(LOG_GUI, "%s: Error setting the voice number: %s.\n", name(), e.what());
		}
	} else {
		// use the parameter as a voice name
		try {
			set_voice(_params[0]);
		} catch(std::invalid_argument &e) {
			PWARNF(LOG_V0, LOG_GUI, "%s: %s.\n", name(), e.what());
			PINFOF(LOG_V0, LOG_GUI, "%s: Using the default system voice.\n", name());
			display_voices();
		} catch(std::runtime_error &e) {
			PERRF(LOG_GUI, "%s: Error setting the voice name: %s.\n", name(), e.what());
		}
	}

	m_rate = cur_rate();
	m_default_vol = cur_vol();
	PDEBUGF(LOG_GUI, LOG_V0, "%s: curr.rate = %d, def.vol. = %d\n", name(), m_rate, m_default_vol);
}

void TTSDev_SAPI::set_voice(int _num)
{
	IEnumSpObjectTokens *pEnum = nullptr;
	HRESULT hr = SpEnumTokens(SPCAT_VOICES, NULL, NULL, &pEnum);
	if(FAILED(hr)) {
		throw std::runtime_error("cannot enumerate the available voices");
	}

	ULONG count = 0;
	hr = pEnum->GetCount(&count);
	if(FAILED(hr)) {
		pEnum->Release();
		throw std::runtime_error("cannot get the number of available voices");
	} else if(count == 0) {
		pEnum->Release();
		throw std::runtime_error("no voices available");
	}

	if(ULONG(_num) > count) {
		pEnum->Release();
		PINFOF(LOG_V0, LOG_GUI, "%s: Number of available voices: %u\n", name(), count);
		throw std::invalid_argument(str_format("the specified voice n. %d is greater than the maximum", _num));
	}

	ISpObjectToken *pToken = nullptr;
	hr = pEnum->Item(_num - 1, &pToken);
	pEnum->Release();

	if(SUCCEEDED(hr)) {
		ISpDataKey *pAttributesKey;
		hr = pToken->OpenKey(L"Attributes", &pAttributesKey);
		if(SUCCEEDED(hr)) {
			WCHAR *pszName = nullptr;
			pAttributesKey->GetStringValue(L"Name", &pszName);
			PINFOF(LOG_V0, LOG_GUI, "  voice n. %d%s:\n", _num, _num<=1 ? " (default)" : "");
			PINFOF(LOG_V0, LOG_GUI, "    name: %s\n", utf8::narrow(pszName).c_str());
			CoTaskMemFree(pszName);
			pAttributesKey->Release();
		}

		hr = m_voice->SetVoice(pToken);
		pToken->Release();

		if(SUCCEEDED(hr)) {
			PINFOF(LOG_V0, LOG_GUI, "%s: Using voice n. %d\n", name(), _num);
		} else {
			throw std::runtime_error(str_format("cannot set voice n. %d", _num));
		}
	} else {
		throw std::runtime_error(str_format("cannot retreive voice n. %d", _num));
	}
}

void TTSDev_SAPI::set_voice(const std::string &_name)
{
	if(_name.empty()) {
		throw std::invalid_argument("no voice has been specified");
	}
	std::wstring wname = utf8::widen(_name);

	ISpObjectTokenCategory *pCategory;
	HRESULT hr = SpGetCategoryFromId(SPCAT_VOICES, &pCategory);
	if(FAILED(hr) || !pCategory) {
		return;
	}

	IEnumSpObjectTokens *pEnum;
	hr = pCategory->EnumTokens(nullptr, nullptr, &pEnum);
	if(FAILED(hr) || !pEnum) {
		pCategory->Release();
		return;
	}

	ISpObjectToken *pToken = nullptr;
	ULONG count = 0;
	hr = pEnum->GetCount(&count);
	if(SUCCEEDED(hr) && count) {

		for(ULONG i = 0; i < count; i++) {
			hr = pEnum->Next(1, &pToken, nullptr);
			if(FAILED(hr)) {
				continue;
			}

			ISpDataKey *pAttributesKey;
			hr = pToken->OpenKey(L"Attributes", &pAttributesKey);
			if(FAILED(hr)) {
				pToken->Release();
				pToken = nullptr;
				continue;
			}

			WCHAR *pszName = nullptr;
			pAttributesKey->GetStringValue(L"Name", &pszName);
			if(wname.compare(pszName) != 0) {
				CoTaskMemFree(pszName);
				pAttributesKey->Release();
				pToken->Release();
				pToken = nullptr;
				continue;
			}

			PINFOF(LOG_V0, LOG_GUI, "  voice n. %u%s:\n", i+1, i==0 ? " (default)" : "");
			PINFOF(LOG_V0, LOG_GUI, "    name: %s\n", utf8::narrow(pszName).c_str());

			CoTaskMemFree(pszName);
			pAttributesKey->Release();

			break;
		}
	} else {
		PWARNF(LOG_V0, LOG_GUI, "  no SAPI voices found!\n");
	}

	pEnum->Release();
	pCategory->Release();

	if(pToken) {
		hr = m_voice->SetVoice(pToken);
		pToken->Release();
		if(FAILED(hr)) {
			throw std::runtime_error("the specified voice cannot be set");
		}
	} else {
		throw std::invalid_argument(str_format("the specified voice '%s' cannot be found", _name.c_str()));
	}
}

void TTSDev_SAPI::speak(const std::string &_text, bool _purge)
{
	check_open();

	std::wstring wtext = utf8::widen(_text);
	uint32_t flags = SPF_ASYNC | SPF_IS_XML;
	if(_purge) {
		flags |= SPF_PURGEBEFORESPEAK;
	}
	PDEBUGF(LOG_V1, LOG_GUI, "%s:\n%s\n", name(), _text.c_str());
	HRESULT hr = m_voice->Speak(wtext.data(), flags, NULL);
	if(FAILED(hr)) {
		throw std::runtime_error(str_format("cannot speak \"%s\"", _text.c_str()));
	}
}

bool TTSDev_SAPI::is_speaking() const
{
	SPVOICESTATUS status;
	HRESULT hr = m_voice->GetStatus(&status, NULL);
	if(!SUCCEEDED(hr) || (status.dwRunningState == SPRS_DONE)) {
		return false;
	}
	return true;
}

void TTSDev_SAPI::stop()
{
	check_open();

	if(!is_speaking()) {
		return;
	}

	try {
		speak("", true);
	} catch(...) {}
}

int TTSDev_SAPI::cur_vol() const
{
	USHORT usVolume;
	HRESULT hr = m_voice->GetVolume(&usVolume);
	if(SUCCEEDED(hr)) {
		return usVolume;
	}
	return 0;
}

int TTSDev_SAPI::cur_rate() const
{
	long rateAdjust;
	HRESULT hr = m_voice->GetRate(&rateAdjust);
	if(SUCCEEDED(hr)) {
		return rateAdjust;
	}
	return 0;
}

bool TTSDev_SAPI::set_volume(int _volume)
{
	check_open();

	_volume = std::clamp(_volume, -10, 10);
	if(_volume == m_volume) {
		return false;
	}

	stop();

	const double vol_step = 100.0 / 20.0;
	int new_vol = m_default_vol + round(double(_volume) * vol_step);
	m_volume = _volume;

	HRESULT hr = m_voice->SetVolume(new_vol);

	PDEBUGF(LOG_V1, LOG_GUI, "%s: def.vol.=%d, vol.=%d, new vol.=%d, cur.vol=%d\n", name(),
			m_default_vol, _volume, new_vol, cur_vol());

	return SUCCEEDED(hr);
}

bool TTSDev_SAPI::set_rate(int _rate)
{
	check_open();

	_rate = std::clamp(_rate, -10, 10);
	if(_rate == m_rate) {
		return false;
	}

	HRESULT hr = m_voice->SetRate(_rate);

	PDEBUGF(LOG_V1, LOG_GUI, "%s: rate adj.=%d, cur.rate=%d\n", name(),
			_rate, cur_rate());

	m_rate = _rate;

	return SUCCEEDED(hr);
}

void TTSDev_SAPI::close()
{
	if(is_open()) {
		m_voice->Release();
		m_voice = nullptr;
		// To close the COM library gracefully on a thread, each successful call to
		// CoInitializeEx, *including any call that returns S_FALSE*, must be balanced
		// by a corresponding call to CoUninitialize.
		CoUninitialize();
	}
}

void TTSDev_SAPI::check_open() const
{
	if(!is_open()) {
		throw std::runtime_error("the device is not open");
	}
}

void TTSDev_SAPI::display_voices() noexcept
{
	ISpObjectTokenCategory *pCategory;
	HRESULT hr = SpGetCategoryFromId(SPCAT_VOICES, &pCategory);
	if(FAILED(hr) || !pCategory) {
		return;
	}

	IEnumSpObjectTokens *pEnum;
	hr = pCategory->EnumTokens(nullptr, nullptr, &pEnum);
	if(FAILED(hr) || !pEnum) {
		pCategory->Release();
		return;
	}

	PINFOF(LOG_V0, LOG_GUI, "%s: List of available voices:\n", name());

	ULONG count = 0;
	hr = pEnum->GetCount(&count);
	if(SUCCEEDED(hr) && count) {
		WCHAR *pszDesc = nullptr;
		WCHAR *pszName = nullptr;
		WCHAR *pszLang = nullptr;
		WCHAR *pszGender = nullptr;
		ISpObjectToken *pToken = nullptr;

		for(ULONG i = 0; i < count; i++) {
			hr = pEnum->Next(1, &pToken, nullptr);
			if(FAILED(hr) || !pToken) {
				break;
			}

			ISpDataKey *pAttributesKey;
			hr = pToken->OpenKey(L"Attributes", &pAttributesKey);
			if(FAILED(hr)) {
				PERRF(LOG_GUI, "  error accessing the voices attributes!\n");
				pToken->Release();
				break;
			}

			pAttributesKey->GetStringValue(L"Name", &pszName);
			pAttributesKey->GetStringValue(L"Language", &pszLang);
			pAttributesKey->GetStringValue(L"Gender", &pszGender);
			SpGetDescription(pToken, &pszDesc);

			PINFOF(LOG_V0, LOG_GUI, "  voice n. %u%s:\n", i+1, i==0 ? " (default)" : "");
			PINFOF(LOG_V0, LOG_GUI, "    name: %s\n", utf8::narrow(pszName).c_str());
			PINFOF(LOG_V0, LOG_GUI, "    language LCID: %s\n", utf8::narrow(pszLang).c_str());
			PINFOF(LOG_V0, LOG_GUI, "    gender: %s\n", utf8::narrow(pszGender).c_str());
			PINFOF(LOG_V0, LOG_GUI, "    description: %s\n", utf8::narrow(pszDesc).c_str());

			CoTaskMemFree(pszDesc);
			CoTaskMemFree(pszName);
			CoTaskMemFree(pszLang);
			CoTaskMemFree(pszGender);

			pAttributesKey->Release();
			pToken->Release();
		}
	} else {
		PWARNF(LOG_V0, LOG_GUI, "  no SAPI voices found!\n");
	}

	pEnum->Release();
	pCategory->Release();
}

#endif