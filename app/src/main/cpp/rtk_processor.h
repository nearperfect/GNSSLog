#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif



    JNIEXPORT jlong JNICALL
    Java_com_example_geodgnss_RTKProcessor_initRtkContext(JNIEnv *env, jobject thiz);


    JNIEXPORT void JNICALL
    Java_com_example_geodgnss_RTKProcessor_initNavigation(JNIEnv *env, jobject thiz);

    JNIEXPORT void JNICALL
    Java_com_example_geodgnss_RTKProcessor_processRtkData(JNIEnv *env, jobject thiz,
                                                      jlong context_handle,
                                                      jbyteArray rtcm_data,
                                                      jobjectArray measurements,
                                                      jlong receiver_time);

    JNIEXPORT void JNICALL
    Java_com_example_geodgnss_RTKProcessor_shutdownRtkContext(JNIEnv *env, jobject thiz,
                                                         jlong context_handle);

#ifdef __cplusplus
}
#endif
