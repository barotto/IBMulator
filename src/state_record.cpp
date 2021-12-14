/*
 * Copyright (C) 2021  Marco Bortolin
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
#include "state_record.h"
#include "utils.h"
#include <regex>
#include <dirent.h>
#include <sys/stat.h>
#include "stb/stb.h"

StateRecord::StateRecord(std::string _basepath, std::string _name)
:
m_path(_basepath + FS_SEP + _name),
m_basefile(m_path + FS_SEP + STATE_FILE_BASE),
m_state(m_basefile)
{
	m_info.name = _name;

	if(!FileSys::is_directory(_basepath.c_str())) {
		throw std::runtime_error("The base directory does not exist or is not accessible");
	}
	if(!FileSys::is_file_readable(_basepath.c_str())) {
		throw std::runtime_error("The base directory cannot be read");
	}

	m_info_path   = m_basefile + ".txt";
	m_ini_path    = m_basefile + ".ini";
	m_state_path  = m_basefile + ".bin";
	m_screen_path = m_basefile + ".png";

	if(FileSys::is_directory(m_path.c_str())) {
		// check state completeness
		if(!FileSys::is_file_readable(m_info_path.c_str())) {
			throw std::runtime_error("The info file cannot be read");
		}
		uint64_t fsize = 0;
		FILETIME mtime;
		if(FileSys::get_file_stats(m_info_path.c_str(), &fsize, &mtime) < 0) {
			throw std::runtime_error(str_format("Error accessing '%s'", m_info_path.c_str()).c_str());
		}
		m_info.mtime = FileSys::filetime_to_time_t(mtime);

		if(fsize) {
			std::ifstream infofile = FileSys::make_ifstream(m_info_path.c_str());
			if(!infofile.is_open()) {
				throw std::runtime_error(str_format("Cannot open '%s' for reading", m_info_path.c_str()).c_str());
			}
			std::string verstr;
			std::getline(infofile, verstr);
			m_info.version = 0;
			if(sscanf(verstr.c_str(), "v%u", &m_info.version) != 1) {
				throw std::runtime_error("Error determining the save record version");
			}
			std::getline(infofile, m_info.user_desc);
			if(infofile.fail()) {
				throw std::runtime_error(str_format("Error reading from '%s'", m_info_path.c_str()).c_str());
			}
			std::stringstream buf;
			buf << infofile.rdbuf();
			m_info.config_desc = buf.str();
		}

		if(!FileSys::is_file_readable(m_ini_path.c_str())) {
			throw std::runtime_error("The ini file cannot be read");
		}
		if(!FileSys::is_file_readable(m_state_path.c_str())) {
			throw std::runtime_error("The state file cannot be read");
		}
		if(!FileSys::is_file_readable(m_screen_path.c_str())) {
			throw std::runtime_error("The screen file cannot be read");
		}
	} else if(FileSys::file_exists(m_path.c_str())) {
		throw std::runtime_error("A file with the same archive name already exists");
	} else {
		try {
			FileSys::create_dir(m_path.c_str());
		} catch(std::exception &) {
			throw std::runtime_error("The archive directory cannot be created");
		}
	}
}

StateRecord::~StateRecord()
{
	if(m_framebuffer) {
		SDL_FreeSurface(m_framebuffer);
	}
}

void StateRecord::set_framebuffer(SDL_Surface *_fb)
{
	if(m_framebuffer) {
		SDL_FreeSurface(m_framebuffer);
	}
	m_framebuffer = _fb;
}

void StateRecord::load()
{
	// INI
	try {
		m_config.parse(m_ini_path);
	} catch(std::exception &e) {
		throw std::runtime_error(str_format("Cannot parse '%s'", m_ini_path.c_str()).c_str());
	}

	// GLOBAL STATE
	try {
		m_state.load(m_state_path);
	} catch(std::exception &) {
		throw std::runtime_error(str_format("Cannot load '%s'", m_state_path.c_str()).c_str());
	}
}

void StateRecord::save()
{
	// SAVE INFO
	std::ofstream infofile = FileSys::make_ofstream(m_info_path.c_str());
	if(!infofile.is_open()) {
		throw std::runtime_error(str_format("Cannot open '%s' for writing", m_info_path.c_str()).c_str());
	}
	infofile << "v" << STATE_RECORD_VERSION << "\n" << m_info.user_desc << "\n" << m_info.config_desc;
	if(infofile.fail()) {
		throw std::runtime_error(str_format("Error writing to '%s'", m_info_path.c_str()).c_str());
	}

	// INI
	try {
		m_config.create_file(m_ini_path);
	} catch(...) {
		throw std::runtime_error(str_format("Cannot create config file '%s'", m_ini_path.c_str()).c_str());
	}

	// GLOBAL STATE
	try {
		m_state.save(m_state_path);
	} catch(...) {
		throw std::runtime_error(str_format("Cannot create state file '%s'", m_state_path.c_str()).c_str());
	}

	// FRAMEBUFFER
	if(m_framebuffer) {
		stbi_write_png_compression_level = 9;
		if(stbi_write_png(m_screen_path.c_str(), m_framebuffer->w, m_framebuffer->h,
				m_framebuffer->format->BytesPerPixel, m_framebuffer->pixels, m_framebuffer->pitch) < 0) {
			throw std::runtime_error(str_format("Cannot save the screen to '%s'", m_screen_path.c_str()).c_str());
		}
	}
}

void StateRecord::remove()
{
	if(!FileSys::is_directory(m_path.c_str())) {
		throw std::runtime_error("The state directory does not exist or is not accessible");
	}

	FileSys::remove(m_info_path.c_str());
	FileSys::remove(m_ini_path.c_str());
	FileSys::remove(m_state_path.c_str());
	FileSys::remove(m_screen_path.c_str());

	// remove disk images
	DIR *dir;
	if((dir = FileSys::opendir(m_path.c_str())) == nullptr) {
		throw std::runtime_error("Cannot open directory for reading\n");
	}

	struct dirent *ent;
	std::regex re("^(" STATE_FILE_BASE "-.*\\.img)$", std::regex::ECMAScript|std::regex::icase);
	while((ent = readdir(dir)) != nullptr) {
		struct stat sb;
		std::string dname = FileSys::to_utf8(ent->d_name);
		std::string fullpath = m_path + FS_SEP + dname;
		if(FileSys::stat(fullpath.c_str(), &sb)!=0 || S_ISDIR(sb.st_mode)) {
			continue;
		}
		if(std::regex_search(dname, re)) {
			FileSys::remove(fullpath.c_str());
		}
	}

	if(closedir(dir) != 0) {
		PWARNF(LOG_V1, LOG_GUI, "Cannot close directory '%s'\n", m_path.c_str());
	}

	if(FileSys::remove(m_path.c_str()) != 0) {
		throw std::runtime_error(str_format("Cannot remove directory '%s'", m_path.c_str()).c_str());
	}

	m_path = "";
}