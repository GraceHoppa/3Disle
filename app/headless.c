#include "headless.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <string.h>

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;

static bool try_display(EGLDisplay d, char *err, size_t errlen) {
  EGLint major, minor;
  if (d == EGL_NO_DISPLAY || !eglInitialize(d, &major, &minor)) return false;
  if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
  const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) {
    // Some surfaceless drivers offer configs with no surface type at all.
    const EGLint any[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    if (!eglChooseConfig(d, any, &cfg, 1, &n) || n < 1) return false;
  }
  const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attr);
  if (ctx == EGL_NO_CONTEXT) return false;
  if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pb);
    if (s == EGL_NO_SURFACE || !eglMakeCurrent(d, s, s, ctx)) {
      eglDestroyContext(d, ctx);
      return false;
    }
  }
  g_dpy = d;
  g_ctx = ctx;
  (void)err, (void)errlen;
  return true;
}

bool headless_init(char *err, size_t errlen) {
  PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  PFNEGLQUERYDEVICESEXTPROC query = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
  // The graphics card first, whichever driver runs it.
  if (get_platform && query) {
    EGLDeviceEXT devs[8];
    EGLint n = 0;
    if (query(8, devs, &n))
      for (int i = 0; i < n; i++)
        if (try_display(get_platform(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL), err, errlen)) return true;
  }
  if (get_platform && try_display(get_platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL), err, errlen)) return true;
  if (try_display(eglGetDisplay(EGL_DEFAULT_DISPLAY), err, errlen)) return true;
  if (err) snprintf(err, errlen, "no EGL display that can do OpenGL ES 3");
  return false;
}

void headless_shutdown(void) {
  if (g_dpy == EGL_NO_DISPLAY) return;
  eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(g_dpy, g_ctx);
  eglTerminate(g_dpy);
  g_dpy = EGL_NO_DISPLAY;
}
