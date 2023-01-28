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

#include "gl_shader_chain.h"
#include "utils.h"
#include "program.h"
#include <set>

GLShaderChain::GLShaderChain(std::string _preset)
{
	PINFOF(LOG_V0, LOG_OGL, "Loading shader preset '%s' ...\n", _preset.c_str());
	try {
		m_preset.load(_preset);
	} catch(std::runtime_error &e) {
		PERRF(LOG_OGL, "Error: %s\n", e.what());
		throw;
	}

	PINFOF(LOG_V1, LOG_OGL, " total passes: %u\n", m_preset.get_shaders().size());

	auto samplers_mode = m_preset.get_samplers_mode();
	PINFOF(LOG_V2, LOG_OGL, " samplers mode: %d\n", samplers_mode);

	bool mipmap_origin = false;

	std::vector<std::string> pass_names;
	for(unsigned n=0; n<m_preset.get_shaders().size(); n++) {

		auto const * preset = &m_preset.get_shader(n);
		PINFOF(LOG_V1, LOG_OGL, "Initializing shader pass %u ...\n", n);
		PINFOF(LOG_V2, LOG_OGL, " shader: %s\n", preset->shader.c_str());
		PINFOF(LOG_V2, LOG_OGL, " alias: \"%s\"\n", preset->alias.c_str());
		PINFOF(LOG_V2, LOG_OGL, " filter_linear: %d\n", preset->filter_linear);
		PINFOF(LOG_V2, LOG_OGL, " float_framebuffer: %d\n", preset->float_framebuffer);
		PINFOF(LOG_V2, LOG_OGL, " srgb_framebuffer: %d\n", preset->srgb_framebuffer);
		PINFOF(LOG_V2, LOG_OGL, " frame_count_mod: %d\n", preset->frame_count_mod);
		PINFOF(LOG_V2, LOG_OGL, " wrap_mode: %s\n", ShaderPreset::ms_wrap_str[preset->wrap_mode].c_str());
		PINFOF(LOG_V2, LOG_OGL, " mipmap_input: %d\n", preset->mipmap_input);
		PINFOF(LOG_V2, LOG_OGL, " scale_type_x: %s\n", ShaderPreset::ms_scale_str[preset->scale_type_x].c_str());
		PINFOF(LOG_V2, LOG_OGL, " scale_x: %f\n", preset->scale_x);
		PINFOF(LOG_V2, LOG_OGL, " scale_type_y: %s\n", ShaderPreset::ms_scale_str[preset->scale_type_y].c_str());
		PINFOF(LOG_V2, LOG_OGL, " scale_y: %f\n", preset->scale_y);

		mipmap_origin = preset->mipmap_input || mipmap_origin;

		GLShaderPass *pass = m_chain.emplace_back(std::make_unique<GLShaderPass>(&m_preset, n)).get();

		pass_names.push_back(pass->get_name());
	}

	PINFOF(LOG_V1, LOG_OGL, "Correctly initialized %u shader pass(es)\n", m_chain.size());

	// load user textures
	std::vector<std::string> texture_names;
	if(!m_preset.get_textures().empty()) {
		PINFOF(LOG_V1, LOG_OGL, "Loading user textures ...\n");
		for(auto & utex : m_preset.get_textures()) {
			PINFOF(LOG_V1, LOG_OGL, " \"%s\": %s\n", utex.name.c_str(), utex.path.c_str());
			m_textures.user.push_back(
				std::make_shared<GLTexture>(utex.name, GLTexture::R8G8B8A8_UNORM, utex.mipmap)
			);
			m_textures.user.back()->create_sampler(utex.wrap_mode, utex.filter_linear);
			m_textures.user.back()->update(utex.path);
			texture_names.push_back(utex.name);
		}
	}

	// analyze the samplers configuration
	PINFOF(LOG_V1, LOG_OGL, "Analyzing the samplers configuration ...\n");
	PINFOF(LOG_V2, LOG_OGL, " Pass names: %s\n", str_implode(pass_names, ",").c_str());
	PINFOF(LOG_V2, LOG_OGL, " Texture names: %s\n", str_implode(texture_names, ",").c_str());

	unsigned history_size = 0;
	bool original_needed = false;
	bool last_pass_fbo = false;
	std::vector<bool> feedbacks(m_chain.size());
	for(auto & pass : m_chain) {
		GLShaderProgram *prg = pass->get_program();

		PINFOF(LOG_V1, LOG_OGL, "  Pass '%s' ...\n", pass->get_name().c_str());
		prg->update_samplers(pass_names, texture_names);

		history_size = std::max(history_size, pass->get_program()->get_history_size());
		if(pass->get_index() == 0 && pass->get_program()->is_source_needed()) {
			original_needed = true;
		} else {
			original_needed = pass->get_program()->is_original_needed() || original_needed;
		}
		for(auto output : prg->get_output_samplers()) {
			if(output->number == int(m_chain.size()-1)) {
				last_pass_fbo = true;
			}
		}
		for(auto feedback : prg->get_feedback_samplers()) {
			if(feedback->number < 0 || feedback->number > int(feedbacks.size())) {
				throw std::runtime_error(str_format("invalid feedback number reference: %d", feedback->number));
			}
			feedbacks[feedback->number] = true;
			if(feedback->number == int(m_chain.size()-1)) {
				last_pass_fbo = true;
			}
		}
	}

	auto viewport_size = g_program.config().get_string(DISPLAY_SECTION, DISPLAY_SHADER_OUTPUT, "native");
	m_viewport_size_max = false;
	if(viewport_size.find("max_") == 0) {
		m_viewport_size_max = true;
		viewport_size = viewport_size.substr(4);
	} else if(viewport_size == "native") {
		viewport_size.clear();
	}
	std::regex resreg("^([0-9]*)x([0-9]*)");
	if(!viewport_size.empty()) {
		std::smatch m;
		if(std::regex_search(viewport_size, m, resreg) && m.size() == 3) {
			m_viewport_size.x = str_parse_int_num(m[1].str());
			m_viewport_size.y = str_parse_int_num(m[2].str());
		} else {
			throw std::runtime_error(str_format("invalid viewport size specified: %s", viewport_size.c_str()));
		}
		last_pass_fbo = true;
		PINFOF(LOG_V1, LOG_OGL, "Viewport size: %dx%d\n", m_viewport_size.x, m_viewport_size.y);
	}

	auto last_preset = m_chain.back()->get_preset();
	if(last_preset.scale_type_x == ShaderPreset::viewport && last_preset.scale_type_y == ShaderPreset::viewport
		&& last_preset.scale_x == 1.0 && last_preset.scale_y == 1.0)
	{
		// assuming native res and no pass references the last pass' feedback, it probably renders directly to backbuffer
	}
	else
	{
		// last pass must render to fbo
		last_pass_fbo = true;
	}

	// create builtin textures and framebuffers
	PINFOF(LOG_V1, LOG_OGL, "Creating builtin textures ...\n");
	PINFOF(LOG_V1, LOG_OGL, " History textures: %u\n", history_size);
	if(original_needed || history_size) {
		m_textures.original = std::make_shared<GLTexture>("Original", GLTexture::R8G8B8A8_UNORM, 
			samplers_mode == ShaderPreset::texture ? m_preset[0].mipmap_input : mipmap_origin
		);
		if(samplers_mode == ShaderPreset::texture) {
			m_textures.original->create_sampler(m_preset[0].wrap_mode, m_preset[0].filter_linear);
		}
	}
	for(unsigned n=1; n<=history_size; n++) {
		m_textures.history.push_back(
			std::make_shared<GLTexture>(str_format("OriginalHistory%u",n), GLTexture::R8G8B8A8_UNORM,
				samplers_mode == ShaderPreset::texture ? m_preset[0].mipmap_input : mipmap_origin
			)
		);
		if(samplers_mode == ShaderPreset::texture) {
			m_textures.history.back()->create_sampler(m_preset[0].wrap_mode, m_preset[0].filter_linear);
		}
	}
	m_history_ready = (history_size == 0);
	m_textures.output.reserve(m_chain.size());
	m_textures.feedback.resize(m_chain.size()); // initialize all possible feedbacks to null pointers
	unsigned feedcount = 0;
	for(unsigned pass=0; pass<m_chain.size()-(!last_pass_fbo); pass++) {
		auto format = m_chain[pass]->get_program()->get_output_format();
		if(format == GLTexture::Format::UNDEFINED) {
			if(m_chain[pass]->get_preset().float_framebuffer) {
				format = GLTexture::Format::R32G32B32A32_SFLOAT;
			} else if(m_chain[pass]->get_preset().srgb_framebuffer) {
				format = GLTexture::Format::R8G8B8A8_SRGB;
			} else {
				format = GLTexture::Format::R8G8B8A8_UNORM;
			}
		}

		bool filter_linear = true;
		ShaderPreset::Wrap wrap_mode = ShaderPreset::Wrap::clamp_to_border;
		bool mipmap = false;
		if(samplers_mode == ShaderPreset::texture) {
			if(pass < m_chain.size() - 1) {
				mipmap = m_preset[pass + 1].mipmap_input;
				wrap_mode = m_preset[pass + 1].wrap_mode;
				filter_linear = m_preset[pass + 1].filter_linear;
			}
		} else {
			mipmap = mipmap_origin;
		}
		m_textures.output.push_back(
			std::make_shared<GLTexture>(str_format("PassOutput%u",pass), format, mipmap)
		);
		if(samplers_mode == ShaderPreset::texture) {
			m_textures.output.back()->create_sampler(wrap_mode, filter_linear);
		}
		if(feedbacks[pass]) {
			m_textures.feedback[pass] = std::make_shared<GLTexture>(str_format("PassFeedback%u",pass), format, mipmap);
			if(samplers_mode == ShaderPreset::texture) {
				m_textures.feedback[pass]->create_sampler(wrap_mode, filter_linear);
			}
			feedcount++;
		}

		m_chain[pass]->create_framebuffer(str_format("PassFramebuffer%u",pass), m_textures.output.back());
		m_fb_ready = false;
	}
	PINFOF(LOG_V1, LOG_OGL, " Output textures: %u\n", m_textures.output.size());
	PINFOF(LOG_V1, LOG_OGL, " Feedback textures: %u\n", feedcount);

	// bind program samplers to textures
	for(auto & pass : m_chain) {
		GLShaderProgram *prg = pass->get_program();

		for(auto & sampler : prg->get_samplers()) {
			switch(sampler.category) {
				case GLShaderProgram::Sampler2D::Category::Original:
					sampler.texture = m_textures.original;
					break;
				case GLShaderProgram::Sampler2D::Category::Source: {
					unsigned passid = pass->get_index();
					if(passid == 0) {
						sampler.texture = m_textures.original;
					} else {
						assert(passid-1 < m_textures.output.size());
						sampler.texture = m_textures.output[passid-1];
					}
					break;
				}
				case GLShaderProgram::Sampler2D::Category::History:
					if(sampler.number == 0) {
						sampler.texture = m_textures.original;
					} else {
						if(sampler.number > int(m_textures.history.size())) {
							throw std::runtime_error(str_format("cannot access original history %d", sampler.number));
						}
						sampler.texture = m_textures.history[sampler.number-1];
					}
					break;
				case GLShaderProgram::Sampler2D::Category::Feedback:
					if(sampler.number >= int(m_chain.size())) {
						throw std::runtime_error(str_format("cannot access pass feedback %d", sampler.number));
					}
					sampler.texture = m_textures.feedback[sampler.number];
					break;
				case GLShaderProgram::Sampler2D::Category::Output:
					if(sampler.number < 0) {
						// the only case is vec4 OutputSize for the size of this pass' output
						if(pass->get_index() == m_chain.size()-1 && !last_pass_fbo) {
							// alias for FinalViewportSize
							prg->add_alias(sampler.size_uniforms, GLShaderProgram::BuiltinUniform::FinalViewportSize);
							continue;
						}
						sampler.number = pass->get_index();
						sampler.tex_uniforms = nullptr;
					} else {
						if(sampler.number >= int(pass->get_index()) && sampler.tex_uniforms) {
							std::string uniforms = str_implode(prg->get_uniform_names(sampler.tex_uniforms), ",");
							throw std::runtime_error(str_format("cannot access PassOutput%d from pass %d using %s", sampler.number, pass->get_index(), uniforms.c_str()));
						}
						if(sampler.number >= int(m_textures.output.size())) {
							throw std::runtime_error(str_format("PassOutput%d does not exist", sampler.number));
						}
					}
					sampler.texture = m_textures.output[sampler.number];
					break;
				case GLShaderProgram::Sampler2D::Category::User:
					if(sampler.number < 0 || sampler.number >= int(m_textures.user.size())) {
						throw std::runtime_error(str_format("cannot access user texture %d", sampler.number));
					}
					sampler.texture = m_textures.user[sampler.number];
					sampler.gl_sampler = sampler.texture->get_gl_sampler();
					continue;
			}
			if(sampler.texture && samplers_mode == ShaderPreset::texture) {
				sampler.gl_sampler = sampler.texture->get_gl_sampler();
			} else {
				sampler.gl_sampler = pass->get_input_sampler();
			}
		}
	}

	// report samplers and parameters to user
	PINFOF(LOG_V1, LOG_OGL, "Shader configuration:\n");
	for(auto & pass : m_chain) {
		GLShaderProgram *prg = pass->get_program();
		GLFramebuffer *fb = pass->get_framebuffer();
		const char *fbformatstr = "";
		if(fb) {
			auto fbformat = fb->get_target()->get_format();
			fbformatstr = GLTexture::get_format_prop(fbformat).str;
		} else {
			fbformatstr = "UNORM (Backbuffer)";
		}
		PINFOF(LOG_V1, LOG_OGL, " pass %u: '%s', format: %s\n", pass->get_index(), pass->get_name().c_str(), fbformatstr);

		if(prg->get_samplers().size()) {
			PINFOF(LOG_V2, LOG_OGL, "  samplers:\n");
			for(auto & sampler : prg->get_samplers()) {
				if(!sampler.texture) {
					continue;
				}
				PINFOF(LOG_V2, LOG_OGL, "   [%s]\n", sampler.texture->get_name().c_str());
				if(sampler.tex_uniforms) {
					for(auto & u : *sampler.tex_uniforms) {
						PINFOF(LOG_V2, LOG_OGL, "     %s\n", prg->get_uniform(u).str().c_str());
					}
				}
				if(sampler.size_uniforms) {
					for(auto & u : *sampler.size_uniforms) {
						PINFOF(LOG_V2, LOG_OGL, "     %s\n", prg->get_uniform(u).str().c_str());
					}
				}
			}
		}

		if(prg->get_parameters().size()) {
			PINFOF(LOG_V2, LOG_OGL, "  parameters:\n");
			for(auto & param : prg->get_parameters()) {
				PINFOF(LOG_V2, LOG_OGL, "   %s\n", param.str().c_str());
				if(param.uniforms) {
					for(auto & uni : *param.uniforms) {
						PINFOF(LOG_V2, LOG_OGL, "    %s\n", prg->get_uniform(uni).str().c_str());
					}
				} else {
					PINFOF(LOG_V2, LOG_OGL, "    UNUSED\n");
				}
			}
		}

		PINFOF(LOG_V2, LOG_OGL, "  builtins:\n");
		auto & builtins = prg->get_builtins();
		for(unsigned b=0; b<GLShaderProgram::BuiltinCount; b++) {
			if(b == GLShaderProgram::Source || b == GLShaderProgram::Original) {
				continue;
			}
			if(!builtins[b].empty()) {
				PINFOF(LOG_V2, LOG_OGL, "   [%s]\n", GLShaderProgram::get_builtin_name(static_cast<GLShaderProgram::BuiltinUniform>(b)));
				for(auto & u : builtins[b]) {
					PINFOF(LOG_V2, LOG_OGL, "     %s\n", prg->get_uniform(u).str().c_str());
				}
			}
		}
	}

	PINFOF(LOG_V0, LOG_OGL, "Filter chain created successfully.\n");
}

void GLShaderChain::init_history(unsigned _width, unsigned _height, GLenum _format, GLenum _type, unsigned _stride, void *_data)
{
	if(!m_history_ready) {
		for(auto & tex : m_textures.history) {
			tex->update(_width, _height, _format, _type, _stride, _data);
		}
		m_history_ready = true;
	}
}

void GLShaderChain::init_framebuffers(const vec2i _original, const vec2i _viewport)
{
	if(!m_fb_ready) {
		update_size(_original.x, _original.y, ShaderPreset::Scale::original, m_textures.output);
		update_size(_original.x, _original.y, ShaderPreset::Scale::original, m_textures.feedback);
		update_size(_viewport.x, _viewport.y, ShaderPreset::Scale::viewport, m_textures.output);
		update_size(_viewport.x, _viewport.y, ShaderPreset::Scale::viewport, m_textures.feedback);
		// width and height act like multipliers, the absolute value is defined in the preset:
		update_size(1, 1, ShaderPreset::Scale::absolute, m_textures.output);
		update_size(1, 1, ShaderPreset::Scale::absolute, m_textures.feedback);
		update_sources_size(m_textures.output);
		update_sources_size(m_textures.feedback);
		m_fb_ready = true;
	}
}

void GLShaderChain::clear_framebuffers()
{
	for(auto & pass : m_chain) {
		auto * fb = pass->get_framebuffer();
		if(fb) {
			fb->clear();
		}
	}
}

void GLShaderChain::update_size(unsigned _w, unsigned _h, ShaderPreset::Scale _prop, TexList & _target)
{
	assert(_prop != ShaderPreset::Scale::source);
	if(_prop == ShaderPreset::viewport && m_viewport_size.x != 0 && m_viewport_size.y != 0) {
		double ratio = double(_w) / double(_h);
		if(m_viewport_size_max) {
			if(_w > _h) {
				if(_h > unsigned(m_viewport_size.y)) {
					_h = m_viewport_size.y;
					_w = round(double(_h) * ratio);
				}
			} else {
				if(_w > unsigned(m_viewport_size.x)) {
					_w = m_viewport_size.x;
					_h = round(double(_w) / ratio);
				}
			}
		} else {
			if(_w > _h) {
				_h = m_viewport_size.y;
				_w = round(double(_h) * ratio);
			} else {
				_w = m_viewport_size.x;
				_h = round(double(_w) / ratio);
			}
		}
	}
	for(size_t pass=0; pass<_target.size(); pass++) {
		bool updated = false;
		GLTexture *outtex = _target[pass].get();
		if(!outtex) {
			continue;
		}
		float outw = outtex->get_size().x;
		float outh = outtex->get_size().y;
		const ShaderPreset::ShaderN *preset = &m_chain[pass]->get_preset();
		if(preset->scale_type_x == _prop) {
			outw = preset->scale_x * _w;
			updated = updated || outtex->get_size().x != outw;
		}
		if(preset->scale_type_y == _prop) {
			outh = preset->scale_y * _h;
			updated = updated || outtex->get_size().y != outh;
		}
		if(updated) {
			outtex->update(unsigned(outw), unsigned(outh));
			m_chain[pass]->get_framebuffer()->size_updated();
		}
	}
}

void GLShaderChain::update_sources_size(TexList & _target)
{
	for(size_t pass=1; pass<_target.size(); pass++) {
		bool updated = false;
		GLTexture *outtex = _target[pass].get();
		if(!outtex) {
			// an empty feedback during the init phase
			continue;
		}
		GLTexture *srctex = m_textures.output[pass-1].get();
		float outw = outtex->get_size().x;
		float outh = outtex->get_size().y;
		const ShaderPreset::ShaderN *preset = &m_chain[pass]->get_preset();
		if(preset->scale_type_x == ShaderPreset::Scale::source) {
			outw = preset->scale_x * srctex->get_size().x;
			updated = updated || outtex->get_size().x != outw;
		}
		if(preset->scale_type_y == ShaderPreset::Scale::source) {
			outh = preset->scale_y * srctex->get_size().y;
			updated = updated || outtex->get_size().y != outh;
		}
		if(updated) {
			outtex->update(unsigned(outw), unsigned(outh));
			m_chain[pass]->get_framebuffer()->size_updated();
		}
	}
}

void GLShaderChain::update_size(unsigned _w, unsigned _h, ShaderPreset::Scale _prop)
{
	update_size(_w, _h, _prop, m_textures.output);
	update_sources_size(m_textures.output);
}

void GLShaderChain::rotate_output_feedbacks()
{
	PDEBUGF(LOG_V3, LOG_OGL, "Rotating output feedbacks\n");
	for(size_t pass=0; pass<m_textures.output.size(); pass++) {
		if(m_textures.feedback[pass]) {
			const auto & cur_size = m_textures.output[pass]->get_size();
			const auto & prev_size = m_textures.feedback[pass]->get_size();
			m_textures.output[pass]->swap(*m_textures.feedback[pass]);
			if(prev_size.x != cur_size.x || prev_size.y != cur_size.y) {
				m_textures.output[pass]->update(cur_size.x, cur_size.y);
			}
			PDEBUGF(LOG_V3, LOG_OGL, "Output %u is now GL:%u\n", pass, m_textures.output[pass]->get_gl_name());
			m_chain[pass]->get_framebuffer()->bind_target();
		}
	}
}

void GLShaderChain::rotate_original_history()
{
	PDEBUGF(LOG_V3, LOG_OGL, "Rotating history textures\n");
	if(m_textures.history.empty()) {
		return;
	}
	for(int i=int(m_textures.history.size())-2; i>=0; i--) {
		m_textures.history[size_t(i)]->swap(*m_textures.history[size_t(i)+1]);
	}
	m_textures.original->swap(*m_textures.history[0]);
}

GLTexture * GLShaderChain::get_original()
{
	return m_textures.original.get();
}

GLTexture * GLShaderChain::get_last_pass_output()
{
	if(m_textures.output.size() == m_chain.size()) {
		return m_textures.output.back().get();
	}
	return nullptr;
}
