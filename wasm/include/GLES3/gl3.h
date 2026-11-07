// The part of the OpenGL ES 3 API this engine calls, for the web build; wasm/glue.js forwards each to
// WebGL2. Generated from the Khronos header by listing what the code uses.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef unsigned int GLenum; typedef unsigned char GLboolean; typedef unsigned int GLbitfield; typedef int GLint; typedef int GLsizei;
typedef unsigned int GLuint; typedef float GLfloat; typedef char GLchar; typedef long GLintptr; typedef long GLsizeiptr; typedef unsigned char GLubyte;

#define GL_ARRAY_BUFFER 0x8892
#define GL_BACK 0x0405
#define GL_BLEND 0x0BE2
#define GL_CCW 0x0901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_COMPILE_STATUS 0x8B81
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_TEST 0x0B71
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FALSE 0
#define GL_FLOAT 0x1406
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_LINK_STATUS 0x8B82
#define GL_NEAREST 0x2600
#define GL_NO_ERROR 0
#define GL_ONE 1
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_R8 0x8229
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_RED 0x1903
#define GL_RENDERBUFFER 0x8D41
#define GL_REPEAT 0x2901
#define GL_RGBA 0x1908
#define GL_RGBA16F 0x881A
#define GL_RGBA8 0x8058
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE2 0x84C2
#define GL_RGBA8 0x8058
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TRIANGLES 0x0004
#define GL_TRUE 1
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_VERTEX_SHADER 0x8B31

void glActiveTexture(GLenum texture);
void glAttachShader(GLuint program, GLuint shader);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void glBindTexture(GLenum target, GLuint texture);
void glBindVertexArray(GLuint array);
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void glClear(GLbitfield mask);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glCompileShader(GLuint shader);
GLuint glCreateProgram(void);
GLuint glCreateShader(GLenum type);
void glCullFace(GLenum mode);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
void glDeleteShader(GLuint shader);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
void glDepthMask(GLboolean flag);
void glDisable(GLenum cap);
void glDisableVertexAttribArray(GLuint index);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
void glEnable(GLenum cap);
void glEnableVertexAttribArray(GLuint index);
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void glFrontFace(GLenum mode);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
void glGenTextures(GLsizei n, GLuint *textures);
void glGenVertexArrays(GLsizei n, GLuint *arrays);
void glGenerateMipmap(GLenum target);
GLenum glGetError(void);
void glGetFloatv(GLenum pname, GLfloat *data);
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params);
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
void glLinkProgram(GLuint program);
void glPixelStorei(GLenum pname, GLint param);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1i(GLint location, GLint v0);
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUseProgram(GLuint program);
void glVertexAttrib4fv(GLuint index, const GLfloat *v);
void glVertexAttribDivisor(GLuint index, GLuint divisor);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
