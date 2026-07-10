#ifdef __ANDROID__

#include <jni.h>
#include <stdlib.h>

extern "C" JNIEXPORT void JNICALL Java_org_ancurio_mkxp_MKXPActivity_nativeExit()
{
    /** The static state needs to be cleared on exit */
    exit(0);
}

#endif