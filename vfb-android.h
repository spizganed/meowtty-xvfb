#ifndef VFB_ANDROID_H
#define VFB_ANDROID_H
/* The screen memory from the AHardwareBuffer of the app (MEOWTTY_XBUF), or NULL. Sets the row length in bytes. */
void *vfbAndroidBuffer(int width, int height, int *stride_bytes);
#endif
