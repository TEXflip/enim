#pragma once

#include <glad/glad.h>

void APIENTRY onGLDebug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* msg, const void* user);
void APIENTRY onGLDebugARB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);