#include "gl_debug_helpers.hpp"

#include <cstdlib>
#include <iomanip>

#include <plog/Log.h>

namespace {

const char* sourceToStr(GLenum source) {
    switch (source) {
        case GL_DEBUG_SOURCE_API_ARB:             return "API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:   return "Window System";
        case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB: return "Shader Compiler";
        case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:     return "Third Party";
        case GL_DEBUG_SOURCE_APPLICATION_ARB:     return "Application";
        default:                                  return "Other";
    }
}

const char* typeToStr(GLenum type) {
    switch (type) {
        case GL_DEBUG_TYPE_ERROR_ARB:               return "ERROR";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: return "Deprecated";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:  return "Undefined Behavior";
        case GL_DEBUG_TYPE_PORTABILITY_ARB:         return "Portability";
        case GL_DEBUG_TYPE_PERFORMANCE_ARB:         return "Performance";
        default:                                    return "Other";
    }
}

const char* severityToStr(GLenum severity) {
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH_ARB:   return "HIGH";
        case GL_DEBUG_SEVERITY_MEDIUM_ARB: return "MEDIUM";
        case GL_DEBUG_SEVERITY_LOW_ARB:    return "LOW";
        default:                           return "NOTE";
    }
}

} // namespace

void APIENTRY onGLDebug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* msg, const void* user) {
    (void)source;
    (void)length;
    (void)user;

    PLOG_DEBUG << "GL [" << (type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "info") << "] type=0x" << std::hex << type << " id=" << std::dec << id << " sev=0x" << std::hex << severity << ": " << msg;
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
        std::abort();
    }
}

void APIENTRY onGLDebugARB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    (void)length;
    (void)userParam;

    PLOG_DEBUG << "GL [" << sourceToStr(source) << "] [" << typeToStr(type) << "] [" << severityToStr(severity) << "] id=" << id << ": " << message;

    if (type == GL_DEBUG_TYPE_ERROR_ARB || severity == GL_DEBUG_SEVERITY_HIGH_ARB) {
        std::abort();
    }
}