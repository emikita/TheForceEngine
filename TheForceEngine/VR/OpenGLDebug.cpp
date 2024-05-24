#include "OpenGLDebug.h"
#include <TFE_System/system.h>
#include <glew/include/GL/glew.h>

namespace gl
{
#define DOCASE(c) case c: return #c

	const char* GetConstStr(uint32_t constant)
	{
		switch (constant)
		{
			DOCASE(GL_NONE);

			DOCASE(GL_RGB4);
			DOCASE(GL_RGB5);
			DOCASE(GL_RGB8);
			DOCASE(GL_RGB10);
			DOCASE(GL_RGB12);
			DOCASE(GL_RGB16);
			DOCASE(GL_RGB16_SNORM);
			DOCASE(GL_RGB16F);
			DOCASE(GL_R11F_G11F_B10F);

			DOCASE(GL_RGBA4);
			DOCASE(GL_RGBA8);
			DOCASE(GL_RGB10_A2);
			DOCASE(GL_RGBA12);
			DOCASE(GL_RGBA16);
			DOCASE(GL_RGBA8_SNORM);
			DOCASE(GL_RGBA16_SNORM);
			DOCASE(GL_RGBA16F);
			DOCASE(GL_SRGB8_ALPHA8);

			DOCASE(GL_DEPTH_COMPONENT16);
			DOCASE(GL_DEPTH_COMPONENT24);
			DOCASE(GL_DEPTH_COMPONENT32);
			DOCASE(GL_DEPTH24_STENCIL8);
			DOCASE(GL_DEPTH_COMPONENT32F);
			DOCASE(GL_DEPTH32F_STENCIL8);

		default:
		{
			static std::string constStr;
			constStr = fmt::format("0x{:X}", constant);
			TFE_WARN("GL", "unknown OpenGL const {}", constStr);
			return constStr.c_str();
		}
		}
	}
}
