extern "C"
{
	#include <psp2/kernel/modulemgr.h>
	#include <psp2/kernel/processmgr.h>
	#include <psp2/kernel/clib.h>
	#include <PVR_PSP2/EGL/eglplatform.h>
	#include <PVR_PSP2/EGL/egl.h>
	#include <PVR_PSP2/gpu_es4/psp2_pvr_hint.h>

	#include <PVR_PSP2/GLES2/gl2.h>
	#include <PVR_PSP2/GLES2/gl2ext.h>

	#include <psp2/ctrl.h>
}

#include <cmath>
#include <fstream>
#include <string>
#include <cstring>

using namespace std;

//SCE
int _newlib_heap_size_user   = 16 * 1024 * 1024;
unsigned int sceLibcHeapSize = 3 * 1024 * 1024;
unsigned int VBO;

//EGL
EGLDisplay Display;
EGLConfig Config;
EGLSurface Surface;
EGLContext Context;
EGLint NumConfigs, MajorVersion, MinorVersion;
EGLint ConfigAttr[] =
{
	EGL_BUFFER_SIZE, EGL_DONT_CARE,
	EGL_DEPTH_SIZE, 16,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_STENCIL_SIZE, 8,
	EGL_SURFACE_TYPE, 5,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE
};
EGLint ContextAttributeList[] = 
{
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

//PIB cube egl stuff

static GLuint program;
static GLuint positionLoc;
static GLuint colorLoc;

static EGLint surface_width, surface_height;

float vertices[] = {
    -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
     0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
     0.0f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f
};   

/* Define our GLSL shader here. They will be compiled at runtime */
const GLchar vShaderStr[] =
// Vertex Shader
    "#version 100\n"
    "attribute vec3 aPos;\n"
    "attribute vec3 aColor;\n"    
    "varying vec3 oColor;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
        "oColor = aColor;\n"
    "}\n";

const GLchar fShaderStr[] = 
// Fragment Shader
        "#version 100\n"
        "precision mediump float;\n"
        "varying vec3 oColor;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vec4(oColor, 1.0);\n"
        "}\n";


GLuint LoadShader(const GLchar *shaderSrc, GLenum type, GLint *size)
{
    GLuint shader;
    GLint compiled;

    sceClibPrintf("Creating Shader...\n");

    shader = glCreateShader(type);

    if(shader == 0)
    {
        sceClibPrintf("Failed to Create Shader\n");
        return 0;
    }

    glShaderSource(shader, 1, &shaderSrc, size);

    sceClibPrintf("Compiling Shader: %s...\n", shaderSrc);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if(!compiled)
    {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);

        if(infoLen > 1)
        {
            char* infoLog = (char*)malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);  // Shader Logs through GLES functions work :D
            sceClibPrintf("Error compiling shader:\n%s\n", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    sceClibPrintf("Shader Compiled!\n");
    return shader;
}

void render(){
    glViewport(0, 0, surface_width, surface_height);
 
    /* Typical render pass */
    glClear(GL_COLOR_BUFFER_BIT); 
 
    /* Enable and bind the vertex information */
    glEnableVertexAttribArray(positionLoc);    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), (void*)0); 

    /* Enable and bind the color information */
    glEnableVertexAttribArray(colorLoc);    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(colorLoc, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), (void*)(3* sizeof(float)));                           

    glUseProgram(program);

    /* Same draw call as in GLES1 */
    glDrawArrays(GL_TRIANGLES, 0 , 3);

    /* Disable attribute arrays */
    glDisableVertexAttribArray(positionLoc);
    glDisableVertexAttribArray(colorLoc);
 
    eglSwapBuffers(Display, Surface);
}

/* How we load shaders */
int initShaders(void)
{
    GLint status;

    GLuint vshader = LoadShader(vShaderStr, GL_VERTEX_SHADER, NULL);
    GLuint fshader = LoadShader(fShaderStr, GL_FRAGMENT_SHADER, NULL);
    program = glCreateProgram();
    if (program)
    {
        glAttachShader(program, vshader);
        glAttachShader(program, fshader);
        glLinkProgram(program);
    
        glGetProgramiv(program, GL_LINK_STATUS, &status);
	sceClibPrintf("Status: %d\n", status);
        if (status == GL_FALSE)    {
            GLchar log[256];
            glGetProgramInfoLog(fshader, 256, NULL, log);
    
            sceClibPrintf("Failed to link shader program: %s\n", log);
    
            glDeleteProgram(program);
            program = 0;
    
            return -1;
        }
    } else {
        sceClibPrintf("Failed to create a shader program\n");
    
        glDeleteShader(vshader);
        glDeleteShader(fshader);
        return -1;
    }
 
    /* Shaders are now in the programs */
    glDeleteShader(fshader);
    glDeleteShader(vshader);
    return 0;
}

/* This handles creating a view matrix for the Vita */
int initvbos(void)
{
    positionLoc = glGetAttribLocation(program, "aPos");
    colorLoc = glGetAttribLocation(program, "aColor");

    /* Generate vertex and color buffers and fill with data */
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                GL_STATIC_DRAW);             
    return 0;
}

void ModuleInit()
{
	sceKernelLoadStartModule("vs0:sys/external/libfios2.suprx", 0, NULL, 0, NULL, NULL);
	sceKernelLoadStartModule("vs0:sys/external/libc.suprx", 0, NULL, 0, NULL, NULL);
	sceKernelLoadStartModule("app0:module/libgpu_es4_ext.suprx", 0, NULL, 0, NULL, NULL);
  	sceKernelLoadStartModule("app0:module/libIMGEGL.suprx", 0, NULL, 0, NULL, NULL);
	sceClibPrintf("Module init OK\n");
}

void PVR_PSP2Init()
{
	PVRSRV_PSP2_APPHINT hint;
  	PVRSRVInitializeAppHint(&hint);
  	PVRSRVCreateVirtualAppHint(&hint);
	sceClibPrintf("PVE_PSP2 init OK.\n");
}

void EGLInit()
{
	EGLBoolean Res;
	Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(!Display)
	{
		sceClibPrintf("EGL display get failed.\n");
		return;
	}

	Res = eglInitialize(Display, &MajorVersion, &MinorVersion);
	if (Res == EGL_FALSE)
	{
		sceClibPrintf("EGL initialize failed.\n");
		return;
	} else {
        sceClibPrintf("EGL version: %d.%d\n", MajorVersion, MinorVersion);
    }

	//PIB cube demo
	eglBindAPI(EGL_OPENGL_ES_API);

	Res = eglChooseConfig(Display, ConfigAttr, &Config, 1, &NumConfigs);
	if (Res == EGL_FALSE)
	{
		sceClibPrintf("EGL config initialize failed.\n");
		return;
	}

	Surface = eglCreateWindowSurface(Display, Config, (EGLNativeWindowType)0, nullptr);
	if(!Surface)
	{
		sceClibPrintf("EGL surface create failed.\n");
		return;
	}

	Context = eglCreateContext(Display, Config, EGL_NO_CONTEXT, ContextAttributeList);
	if(!Context)
	{
		sceClibPrintf("EGL content create failed.\n");
		return;
	}

	eglMakeCurrent(Display, Surface, Surface, Context);

	// PIB cube demo
	eglQuerySurface(Display, Surface, EGL_WIDTH, &surface_width);
	eglQuerySurface(Display, Surface, EGL_HEIGHT, &surface_height);
	sceClibPrintf("Surface Width: %d, Surface Height: %d\n", surface_width, surface_height);
	glClearDepthf(1.0f);
	glClearColor(0.0f,0.0f,0.0f,1.0f); // You can change the clear color to whatever
	 
	glEnable(GL_CULL_FACE);

	sceClibPrintf("EGL init OK.\n");

    const GLubyte *renderer = glGetString( GL_RENDERER ); 
    const GLubyte *vendor = glGetString( GL_VENDOR ); 
    const GLubyte *version = glGetString( GL_VERSION ); 
    const GLubyte *glslVersion = glGetString( GL_SHADING_LANGUAGE_VERSION ); 
    
    sceClibPrintf("GL Vendor            : %s\n", vendor); 
    sceClibPrintf("GL Renderer          : %s\n", renderer); 
    sceClibPrintf("GL Version (string)  : %s\n", version); 
    sceClibPrintf("GLSL Version         : %s\n", glslVersion);    
}

void EGLEnd()
{
	eglDestroySurface(Display, Surface);
  	eglDestroyContext(Display, Context);
  	eglTerminate(Display);
	sceClibPrintf("EGL terminated.\n");
}

void SCEInit()
{
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceClibPrintf("SCE init OK\n");
}

int main()
{
	ModuleInit();
	PVR_PSP2Init();
	EGLInit();

	SCEInit();
	sceClibPrintf("All init OK.\n");

	initShaders();
	initvbos();
	
	while(1)
	{
	    render();
	}

	EGLEnd();

	sceKernelExitProcess(0);
	return 0;
}