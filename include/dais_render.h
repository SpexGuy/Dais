#ifndef DAIS_RENDER_H_
#define DAIS_RENDER_H_

#include <glad/glad.h>

#define GLSL_VERSION "#version 400\n"
#define MULTILINE_STR(src) #src
#define GLSL(src) GLSL_VERSION #src

#define CheckGLError() CheckGLError_(__FILE__,__LINE__)

static
bool CheckGLError_(const char *File, int Line) {
    bool HasError = false;
    GLenum ErrorCode = glGetError();

    while (ErrorCode != GL_NO_ERROR) {
        HasError = true;
        const char *ErrorName = 0;

        switch(ErrorCode) {
        case GL_INVALID_OPERATION:
            ErrorName = "GL_INVALID_OPERATION";
            break;
        case GL_INVALID_ENUM:
            ErrorName = "GL_INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            ErrorName = "GL_INVALID_VALUE";
            break;
        case GL_OUT_OF_MEMORY:
            ErrorName = "GL_OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            ErrorName = "GL_INVALID_FRAMEBUFFER_OPERATION";
            break;
        }

        if (ErrorName) {
            printf("%s - %s:%d\n", ErrorName, File, Line);
        } else {
            printf("GL error code %d - %s:%d\n", ErrorCode, File, Line);
        }

        ErrorCode = glGetError();
    }
    return HasError;
}

static
void CheckShaderError(GLuint Shader) {
    GLint Success = 0;
    glGetShaderiv(Shader, GL_COMPILE_STATUS, &Success);
    if (!Success) {
        printf("Shader Compile Failed.\n");

        GLint LogSize = 0;
        glGetShaderiv(Shader, GL_INFO_LOG_LENGTH, &LogSize);
        if (LogSize == 0) {
            printf("No log found.\n");
        } else {
            // new is not normally ok in dais, but we don't keep the
            // pointer around long enough to hotswap so it's technically
            // ok here.
            char *Log = new char[LogSize];
            glGetShaderInfoLog(Shader, LogSize, &LogSize, Log);
            printf("%s\n", Log);
            delete[] Log;
        }
    }
}

static
void CheckLinkError(GLuint Program) {
    GLint Success = 0;
    glGetProgramiv(Program, GL_LINK_STATUS, &Success);
    if (!Success) {
        printf("Shader Link Failed.\n");

        GLint LogSize = 0;
        glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &LogSize);
        if (LogSize == 0) {
            printf("No log found.\n");
        } else {
            // new is not normally ok in dais, but we don't keep the
            // pointer around long enough to hotswap so it's technically
            // ok here.
            char *Log = new char[LogSize];
            glGetProgramInfoLog(Program, LogSize, &LogSize, Log);
            printf("%s\n", Log);
            delete[] Log;
        }
    }
}

static
GLuint CompileShader(const char *VertSrc, const char *FragSrc) {
    GLuint Vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(Vertex, 1, &VertSrc, 0);
    glCompileShader(Vertex);
    CheckShaderError(Vertex);

    GLuint Fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(Fragment, 1, &FragSrc, 0);
    glCompileShader(Fragment);
    CheckShaderError(Fragment);

    GLuint Shader = glCreateProgram();
    glAttachShader(Shader, Vertex);
    glAttachShader(Shader, Fragment);
    glLinkProgram(Shader);
    CheckLinkError(Shader);

    return Shader;
}

#endif // DAIS_RENDER_H_
