/*
 * The app half of vfb-android.c (MIT): a JNI library for the MeowTTY app. XBuffer.send(HardwareBuffer, fd) sends the
 * buffer through a connected unix socket (AHardwareBuffer_sendHandleToUnixSocket). Java has no call for that.
 */
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <jni.h>

static jboolean
xbuf_send(JNIEnv *env, jclass cls, jobject hardware_buffer, jint fd)
{
    AHardwareBuffer *buffer = AHardwareBuffer_fromHardwareBuffer(env, hardware_buffer);
    (void) cls;
    return buffer && AHardwareBuffer_sendHandleToUnixSocket(buffer, fd) == 0;
}

JNIEXPORT jint
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    jclass cls;
    static const JNINativeMethod methods[] = {
        { "send", "(Landroid/hardware/HardwareBuffer;I)Z", (void *) xbuf_send },
    };
    (void) reserved;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    cls = (*env)->FindClass(env, "com/spizganed/meowtty/XBuffer");
    if (!cls || (*env)->RegisterNatives(env, cls, methods, 1) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
