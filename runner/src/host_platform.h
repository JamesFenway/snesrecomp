/*
 * host_platform.h — which kind of device the host is built for.
 *
 * Android has been spelled `__ANDROID__` at each site that differs. iOS
 * differs at the same sites for the same reasons (no desktop OpenGL, no
 * process spawning, a read-only app bundle), so those sites test the
 * capability named here instead of growing a second platform test each.
 */
#ifndef SNESRECOMP_HOST_PLATFORM_H
#define SNESRECOMP_HOST_PLATFORM_H

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

/* iOS and iPadOS (TARGET_OS_IPHONE is also set for tvOS/visionOS, which
 * no port targets). */
#if defined(__APPLE__) && TARGET_OS_IPHONE
#  define SNESRECOMP_HOST_IOS 1
#else
#  define SNESRECOMP_HOST_IOS 0
#endif

/* The desktop OpenGL presenter (opengl.c, glsl_shader.c) is not built:
 * Android presents through SDL_Renderer on GLES, iOS through SDL_Renderer
 * on Metal. */
#if defined(__ANDROID__) || SNESRECOMP_HOST_IOS
#  define SNESRECOMP_HOST_NO_DESKTOP_GL 1
#else
#  define SNESRECOMP_HOST_NO_DESKTOP_GL 0
#endif

/* The system starts the app; it cannot start a copy of itself. */
#if defined(__ANDROID__) || SNESRECOMP_HOST_IOS
#  define SNESRECOMP_HOST_NO_SPAWN 1
#else
#  define SNESRECOMP_HOST_NO_SPAWN 0
#endif

#endif /* SNESRECOMP_HOST_PLATFORM_H */
