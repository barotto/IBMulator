/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_GUI_H
#define IBMULATOR_GUI_H

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <Rocket/Core/Input.h>
#include <Rocket/Core/Types.h>
#include "matrix.h"
#include <atomic>

#define GUI_OPENGL_MAJOR_VER 3
#define GUI_OPENGL_MINOR_VER 3

const char * GetGLErrorString(GLenum _error_code);

#define OGL_NO_ERROR_CHECKING 0
#define OGL_ARB_DEBUG_OUTPUT 1
#define OGL_GET_ERROR 2

#define GUI_STOP_ON_ERRORS 1
#define GUI_ARB_DEBUG_OUTPUT_LIMIT 1000
#define GUI_GL_GHOSTHUNTING true

//#define OGL_DEBUG_TYPE OGL_NO_ERROR_CHECKING
//#define OGL_DEBUG_TYPE OGL_ARB_DEBUG_OUTPUT
#define OGL_DEBUG_TYPE OGL_GET_ERROR

enum GUIMode {
	GUI_MODE_NORMAL,
	GUI_MODE_COMPACT,
	GUI_MODE_REALISTIC
};

enum DisplaySampler {
	DISPLAY_SAMPLER_NEAREST,
	DISPLAY_SAMPLER_BILINEAR,
	DISPLAY_SAMPLER_BICUBIC,
};

enum DisplayAspect {
	DISPLAY_ASPECT_ORIGINAL,
	DISPLAY_ASPECT_ADAPTIVE,
	DISPLAY_ASPECT_SCALED
};

#define JOY_NONE INT_MAX

class GUI;
extern GUI g_gui;

class Machine;
class TextModeInfo;

class RocketRenderer;
class RocketSystemInterface;
class RocketFileInterface;

namespace Rocket {
namespace Core {
	class Context;
	class Element;
	class ElementDocument;
}
namespace Controls {
	class ElementFormControl;
}
}

#define RC Rocket::Core
#define RCN Rocket::Controls

#include "window.h"
#include "windows/desktop.h"
#include "windows/normal_interface.h"
#include "windows/realistic_interface.h"
#include "windows/status.h"

//debug
#include "windows/sysdebugger.h"
#include "windows/devstatus.h"
#include "windows/stats.h"

#include "appconfig.h"

enum MouseTypes {
	MOUSE_TYPE_NONE,
	MOUSE_TYPE_PS2,
	MOUSE_TYPE_IMPS2, //Microsoft IntelliMouse
	MOUSE_TYPE_SERIAL,
	MOUSE_TYPE_SERIAL_WHEEL,
	MOUSE_TYPE_SERIAL_MSYS
};

extern ini_enum_map_t g_mouse_types;

#include "hardware/devices/vgadisplay.h"



class GUI
{
private:

	Machine *m_machine;
	Mixer *m_mixer;

	std::string m_assets_path;
	int m_width;
	int m_height;
	int m_half_width;
	int m_half_height;
	SDL_Window *m_SDL_window;
	SDL_DisplayMode m_SDL_fullmode;
	SDL_DisplayMode m_SDL_windowmode;
	SDL_GLContext m_SDL_glcontext;
	SDL_Renderer * m_SDL_renderer;
	std::string m_wnd_title;
	std::string m_curr_title;
	SDL_TimerID m_second_timer;
	std::vector<SDL_Joystick*> m_SDL_joysticks;
	int m_joystick0;
	int m_joystick1;

	bool m_gui_visible;
	bool m_input_grab;
	std::string m_grab_method;
	uint m_mode;

	struct Mouse {
		bool grab;
		bool warped;

		Mouse() :
			grab(true),
			warped(false)
		{}
	} m_mouse;

	double m_symspeed_factor;

	class Display {
	public:
		VGADisplay vga;
		vec2i vga_res;
		std::atomic<bool> vga_updated;
		GLuint tex;
		std::vector<uint32_t> tex_buf;
		GLuint sampler;
		GLint  glintf;
		GLenum glf;
		GLenum gltype;
		GLuint prog;
		GLuint vb;
		GLfloat vb_data[18];
		mat4f projmat;
		mat4f mvmat;
		uint aspect;
		uint scaling;
		float brightness;
		float contrast;
		float saturation;
		vec2i size; // size in pixel of the destination quad

		struct {
			GLint ch0;
			GLint brightness;
			GLint contrast;
			GLint saturation;
			GLint mvmat;
			GLint size;
		} uniforms;

		Display();

	} m_display;

	GLuint m_tex_width;
	GLuint m_tex_height;
	GLuint m_screen_width;
	GLuint m_screen_height;

	RocketRenderer * m_rocket_renderer;
	RocketSystemInterface * m_rocket_sys_interface;
	RocketFileInterface * m_rocket_file_interface;
	Rocket::Core::Context * m_rocket_context;

	class Windows {
	public:
		bool visible;
		bool debug_wnds;
		bool status_wnd;

		Desktop * desktop;
		Interface * interface;
		SysDebugger * debugger;
		Stats * stats;
		Status * status;
		DevStatus * devices;

		Windows();
		void init(Machine *_machine, GUI *_gui, Mixer *_mixer, uint _mode);
		void update();
		void shutdown();
		void toggle_dbg();
		void show(bool _value);
		void toggle();
		bool needs_input();
		void invert_visibility();

	} m_windows;

	void create_window(const char * _title, int _width, int _height, int _flags);
	void check_device_caps();
	void init_Rocket();
	void render_vga();
	void update_window_size(int _w, int _h);
	void update_display_size();
	void update_display_size_realistic();
	void toggle_input_grab();
	void input_grab(bool _value);
	void toggle_fullscreen();

	int m_gl_errors_count;
	static void GL_debug_output(GLenum source, GLenum type, GLuint id,
			GLenum severity, GLsizei length,
			const GLchar* message, GLvoid* userParam);


	void dispatch_hw_event(const SDL_Event &_event);
	void dispatch_rocket_event(const SDL_Event &event);
	void dispatch_window_event(const SDL_WindowEvent &_event);
	bool dispatch_special_keys(const SDL_Event &_event, SDL_Keycode &_discard_next_key);
	void load_splash_image();

	static Uint32 every_second(Uint32 interval, void *param);

	void shutdown_SDL();

	static std::string load_shader_file(const std::string &_path);
	static std::vector<GLuint> attach_shaders(const std::vector<std::string> &_sh_paths, GLuint _sh_type, GLuint _program);

public:
	static std::map<std::string, uint> ms_gui_modes;

public:
	GUI();
	~GUI();

	void init(Machine *_machine, Mixer *_mixer);
	void render();
	void dispatch_event(const SDL_Event &_event);
	void update();
	void shutdown();

	Rocket::Core::ElementDocument * load_document(const std::string &_filename);
	static GLuint load_GLSL_program(const std::vector<std::string> &_vs_path, std::vector<std::string> &_fs_path);
	static std::string get_shaders_dir();
	inline mat4f & get_proj_matrix() { return m_display.projmat; }

	void save_framebuffer(std::string _screenfile, std::string _palfile);
	void show_message(const char* _mex);

	inline void vga_update() { m_display.vga_updated.store(true); }

	void set_audio_volume(float _volume);
	void set_video_brightness(float _level);
	void set_video_contrast(float _level);
	void set_video_saturation(float _level);
};



#if GUI_STOP_ON_ERRORS
	#define GUI_PRINT_ERROR_FUNC PERRFEX_ABORT
#else
	#define GUI_PRINT_ERROR_FUNC PERRFEX
#endif
#if !defined(NDEBUG) && (OGL_DEBUG_TYPE == OGL_GET_ERROR)
	#if defined(GUI_GL_GHOSTHUNTING)
	#define GLCALL(X) \
	{\
		GLenum glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_GUI, "ghost GL Error: %d (%s)\n",glerrcode,GetGLErrorString(glerrcode));\
		X;\
		glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_GUI, #X" GL Error: %d (%s)\n",glerrcode,GetGLErrorString(glerrcode));\
	}
	#define GLCALL_NOGHOST(X) \
	{\
		X;\
		GLenum glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_GUI, #X" GL Error: %d (%s)\n",glerrcode,GetGLErrorString(glerrcode));\
	}
	#else
	#define GLCALL(X) \
	{\
		X;\
		GLenum glerrcode = glGetError();\
		if(glerrcode != GL_NO_ERROR)\
			GUI_PRINT_ERROR_FUNC(LOG_GUI, #X" GL Error: %d (%s)\n",glerrcode,GetGLErrorString(glerrcode));\
	}
	#define GLCALL_NOGHOST(X) GLCALL(X)
	#endif
#else
	#define GLCALL(X) X
	#define GLCALL_NOGHOST(X) X
#endif




#endif
