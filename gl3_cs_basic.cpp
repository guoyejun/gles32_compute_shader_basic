#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>

#include <WindowSurface.h>
#include <EGLUtils.h>

using namespace android;

static const char gComputeShader[] = 
    "#version 320 es\n"
    "layout(local_size_x = 8) in;\n"
    "layout(binding = 0) readonly buffer Input0 {\n"
    "    vec4 data[];\n"
    "} input0;\n"
    "layout(binding = 1) readonly buffer Input1 {\n"
    "    vec4 data[];\n"
    "} input1;\n"
    "layout(binding = 2) writeonly buffer Output {\n"
    "    vec4 data[];\n"
    "} output0;\n"
    "void main()\n"
    "{\n"
    "    uint idx = gl_GlobalInvocationID.x;\n"
    "    vec4 v = input0.data[idx] + input1.data[idx];"
    "    output0.data[idx] = v;\n"
    "}\n";

GLuint arraySize = 16;

#define CHECK() \
{\
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) \
    {\
        printf("glGetError returns %d\n", err); \
    }\
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createComputeProgram(const char* pComputeSource) {
    GLuint computeShader = loadShader(GL_COMPUTE_SHADER, pComputeSource);
    if (!computeShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, computeShader);
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

void setupSSBufferObject(GLuint& ssbo, GLuint index)
{
    GLuint countInByte = arraySize * 4 * sizeof(float);
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, countInByte, NULL, GL_STATIC_DRAW);

    GLint bufMask = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
    float* p = (float*)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, countInByte, bufMask);
    for (GLuint i = 0; i < arraySize; ++i)
    {
        p[4*i] = 2501.0;  //magic number
        p[4*i+1] = 0.2;
        p[4*i+2] = 0.3;
        p[4*i+3] = 0.4;
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, index, ssbo);
}

void tryComputeShader()
{
    GLuint computeProgram;
    GLuint input0SSbo;
    GLuint input1SSbo;
    GLuint outputSSbo;

    CHECK();
    computeProgram = createComputeProgram(gComputeShader);
    CHECK();
    setupSSBufferObject(input0SSbo, 0);
    setupSSBufferObject(input1SSbo, 1);
    setupSSBufferObject(outputSSbo, 2);
    CHECK();

    glUseProgram(computeProgram);
    glDispatchCompute(2,1,1);
    CHECK();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputSSbo);
    float* pOut = (float*)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, arraySize * 4 * sizeof(float), GL_MAP_READ_BIT);
    for (GLuint i = 0; i < arraySize; ++i)
    {
        if (fabs(pOut[i*4] - 5002.) > 0.0001 ||
            fabs(pOut[i*4+1] - 0.4) > 0.0001 ||
            fabs(pOut[i*4+2] - 0.6) > 0.0001 ||
            fabs(pOut[i*4+3] - 0.8) > 0.0001) {
          printf("verification FAILED at array index %d\n", i);
          glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
          return;
        }
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    printf("verification PASSED\n");
}

int main(int /*argc*/, char** /*argv*/) {
    EGLBoolean returnValue;
    EGLConfig myConfig = {0};

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLint s_configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_NONE };
    EGLint majorVersion;
    EGLint minorVersion;
    EGLContext context;
    EGLSurface surface;
    EGLint w, h;

    EGLDisplay dpy;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
        return 0;
    }

    returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
    if (returnValue != EGL_TRUE) {
        printf("eglInitialize failed\n");
        return 0;
    }

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();
    returnValue = EGLUtils::selectConfigForNativeWindow(dpy, s_configAttribs, window, &myConfig);
    if (returnValue) {
        printf("EGLUtils::selectConfigForNativeWindow() returned %d", returnValue);
        return 0;
    }

    surface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
    if (surface == EGL_NO_SURFACE) {
        printf("gelCreateWindowSurface failed.\n");
        return 0;
    }

    context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return 0;
    }
    returnValue = eglMakeCurrent(dpy, surface, surface, context);
    if (returnValue != EGL_TRUE) {
        printf("eglMakeCurrent failed returned %d\n", returnValue);
        return 0;
    }

    tryComputeShader();

    return 0;
}
