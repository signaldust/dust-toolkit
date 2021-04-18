
#pragma once

// FIXME: this only works on macOS for now.. need to setup gl3w for Windows..
#if defined(__APPLE__)
# include <opengl/gl3.h>
#elif defined(_WIN32)
# include <GL/gl3w.h>
#else
# error "FIXME: GL headers for platform"
#endif

#include <vector>

#include "dust/core/defs.h"

namespace dust
{
    // Compiles a combined shader into a program.
    //
    // Prefix should begin with #version, but can contain other things
    // that should be placed before the main shader.
    //
    // Put vertex shader inside "#ifdef VERTEX" block.
    // Put (optional) geometry shader inside "#ifdef GEOMETRY" block.
    // Put (optional) fragment shader inside "#ifdef FRAGMENT" block.
    //
    // Defines "vs_gs(x)", "vs_fs(x)", "gs_fs(x)" macros to define
    // input/output variables between shader stages.
    //
    // return true on success, false on errors
    // if errors happen, errorCallback is called with a c-string
    template <typename ErrorFN>
    static GLuint compileShaderGL(ErrorFN & errorCallback,
        const char * text, const char * prefix = "#version 410 core\n")
    {
        // add line directives..
        // sadly the "filename" part is not standard
        // and at least Apple just refuses
        const char * lineVsShader =
            "\n#define VERTEX"
            "\n#define vs_gs(x) out x;"
            "\n#define vs_fs(x) out x;"
            "\n#define gs_fs(x) "
            "\n#line 1\n";
        const char * lineGsShader =
            "\n#define GEOMETRY"
            "\n#define vs_gs(x) in x[];"
            "\n#define vs_fs(x) "
            "\n#define gs_fs(x) out x;"
            "\n#line 1\n";
        const char * lineFsShader =
            "\n#define FRAGMENT"
            "\n#define vs_gs(x) "
            "\n#define vs_fs(x) in x;"
            "\n#define gs_fs(x) in x;"
            "\n#line 1\n";
    
        const char * vsPtrs[] = { prefix, lineVsShader, text };
        const char * gsPtrs[] = { prefix, lineGsShader, text };
        const char * fsPtrs[] = { prefix, lineFsShader, text };

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 3, vsPtrs, 0);
        glCompileShader(vs);
        {
            GLint success = 0;
            glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
            if(!success)
            {
                debugPrint("Error compiling shader (VS):\n");

                const char * line = text;
                for(int i = 1; *line; ++i)
                {
                    const char * next = strchr(line, '\n');
                    if(!next)
                    {
                        printf("%d:\t %s\n", i, line); break;
                    }
                    else
                    {
                        printf("%d:\t %.*s\n", i, int(next - line), line);
                        line = next + 1;
                    }
                }
                
                GLint logSize = 0;
                glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logSize);

                std::vector<char> errorOut(logSize + 1);
                glGetShaderInfoLog(vs, logSize, &logSize, errorOut.data());
                errorCallback(errorOut.data());

                glDeleteShader(vs);
                return false;
            }
        }
        
        // build program
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glDeleteShader(vs);

        if(strstr(text, "GEOMETRY"))
        {
            GLuint gs = glCreateShader(GL_GEOMETRY_SHADER);
            glShaderSource(gs, 3, gsPtrs, 0);
            glCompileShader(gs);
            {
                GLint success = 0;
                glGetShaderiv(gs, GL_COMPILE_STATUS, &success);
                if(!success)
                {
                    debugPrint("Error compiling shader (GS):\n");
    
                    const char * line = text;
                    for(int i = 1; *line; ++i)
                    {
                        const char * next = strchr(line, '\n');
                        if(!next)
                        {
                            printf("%d:\t %s\n", i, line); break;
                        }
                        else
                        {
                            printf("%d:\t %.*s\n", i, int(next - line), line);
                            line = next + 1;
                        }
                    }
                    
                    GLint logSize = 0;
                    glGetShaderiv(gs, GL_INFO_LOG_LENGTH, &logSize);
                    
                    std::vector<char> errorOut(logSize + 1);
                    glGetShaderInfoLog(gs, logSize, &logSize, errorOut.data());
                    errorCallback(errorOut.data());
    
                    glDeleteShader(gs);
                    glDeleteProgram(prog);
                    return 0;
                }
            }
    
            glAttachShader(prog, gs);
            glDeleteShader(gs);
        }
        
        if(strstr(text, "FRAGMENT"))
        {
            GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fs, 3, fsPtrs, 0);
            glCompileShader(fs);
            {
                GLint success = 0;
                glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
                if(!success)
                {
                    debugPrint("Error compiling shader (FS):\n");
    
                    const char * line = text;
                    for(int i = 1; *line; ++i)
                    {
                        const char * next = strchr(line, '\n');
                        if(!next)
                        {
                            printf("%d:\t %s\n", i, line); break;
                        }
                        else
                        {
                            printf("%d:\t %.*s\n", i, int(next - line), line);
                            line = next + 1;
                        }
                    }
                    
                    GLint logSize = 0;
                    glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logSize);
                    
                    std::vector<char> errorOut(logSize + 1);
                    glGetShaderInfoLog(fs, logSize, &logSize, errorOut.data());
                    errorCallback(errorOut.data());
    
                    glDeleteShader(fs);
                    glDeleteProgram(prog);
                    return 0;
                }
            }
    
            glAttachShader(prog, fs);
            glDeleteShader(fs);
        }

        // link
        glLinkProgram(prog);
        
        GLint success = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &success);
        if(!success)
        {
            GLint logSize = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logSize);

            std::vector<char> errorOut(logSize + 1);
            glGetProgramInfoLog(prog, logSize, &logSize, errorOut.data());
            errorCallback(errorOut.data());

            glDeleteProgram(prog);
            return 0;
        }

        return prog;
    }

};