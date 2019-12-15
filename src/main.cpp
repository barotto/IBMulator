/*
 * Copyright (C) 2015-2019  Marco Bortolin
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
#include "filesys.h"

#include <sstream>
#include <SDL.h>


int main(int argc, char** argv)
{
	// start the SysLog thread as the very first operation so the user can be
	// notified of possible errors.
	std::thread syslog(&Syslog::start, &g_syslog);
	
	std::stringstream ss;
	LogStream * templog = new LogStream(ss, true);
	g_syslog.add_device(LOG_ALL_PRIORITIES, LOG_ALL_FACILITIES, templog);
	g_syslog.set_verbosity(DEFAULT_LOG_VERBOSITY);

	PINFO(LOG_V0, "Program start\n");
	
	bool start = true;
	int return_value = 0;

	try {
		if(!g_program.initialize(argc,argv)) {
			PINFO(LOG_V0, "Manual configuration required\n");
			start = false;
			return_value = 0;
		}
	} catch(std::exception &e) {
		std::string message = "A problem occurred during initialisation.\n";
		std::string logfile = g_program.config().get_cfg_home() + FS_SEP "log.txt";
		if(FileSys::file_exists(logfile.c_str())) {
			PERR("exception caught during initialization! giving up :(\n");
			message += "See the log file for more info"
#ifndef _WIN32
				", or start the program in a terminal"
#endif
				".\n"
				"Use the -v NUM command line switch to enable verbose logging.\n\n"
				"The log file is here:\n" + g_program.config().get_cfg_home() + FS_SEP"log.txt\n"
				"The ini file is here:\n" + g_program.config().get_parsed_file();
		} else {
			message += "Log content:\n";
			message += ss.str();
		}
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialisation error",
				message.c_str(),
				nullptr);
		
		start = false;
		return_value = 1;
	}

	g_syslog.remove(templog, false);

	if(start) {
		g_program.start();
	}

	PINFO(LOG_V0, "Program stop\n");

	g_syslog.cmd_quit();
	syslog.join();
	
	return return_value;
}
