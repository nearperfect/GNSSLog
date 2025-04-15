#include "rtk_processor.h"
#include <android/log.h>
#include "rtklib.h"  // Your RTK library header

static JavaVM *jvm = nullptr;
static jclass class_RTKProcessor;
static jmethodID method_updatePosition;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    jvm = vm;
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass cls = env->FindClass("com/example/geodgnss/RTKProcessor");
    class_RTKProcessor = static_cast<jclass>(env->NewGlobalRef(cls));
    method_updatePosition = env->GetMethodID(cls, "updatePosition", "(DDD)V");

    return JNI_VERSION_1_6;
}

struct RtkContext {
    rtklib::rtk_t rtk;
    JNIEnv *env;
    jobject obj;
};

JNIEXPORT jlong JNICALL
Java_com_example_geodgnss_RTKProcessor_initRtkContext(JNIEnv *env, jobject thiz) {
    auto *ctx = new RtkContext();
    // Initialize RTKLIB context
    rtklib::init_rtk(&ctx->rtk);
    return reinterpret_cast<jlong>(ctx);
}

JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_processRtkData(JNIEnv *env, jobject thiz,
                                                     jlong context_handle,
                                                     jbyteArray rtcm_data,
                                                     jobjectArray measurements,
                                                     jlong receiver_time) {
    auto *ctx = reinterpret_cast<RtkContext*>(context_handle);

    // Process RTCM data
    jbyte *rtcm_bytes = env->GetByteArrayElements(rtcm_data, nullptr);
    jsize rtcm_len = env->GetArrayLength(rtcm_data);
    rtklib::input_rtcm3(&ctx->rtk, reinterpret_cast<uint8_t*>(rtcm_bytes), rtcm_len);
    env->ReleaseByteArrayElements(rtcm_data, rtcm_bytes, JNI_ABORT);

    // Process GNSS measurements
    jsize num_meas = env->GetArrayLength(measurements);
    for (jsize i = 0; i < num_meas; i++) {
        jobject meas_obj = env->GetObjectArrayElement(measurements, i);
        // Convert GnssMeasurement to RTKLIB obs structure
        rtklib::obsd_t obs = convert_gnss_measurement(env, meas_obj);
        rtklib::update_obs(&ctx->rtk, &obs);
        env->DeleteLocalRef(meas_obj);
    }

    // Execute RTK processing
    try {
        rtklib::rtk_solve(&ctx->rtk);
    } catch (const std::exception &e) {
        __android_log_print(ANDROID_LOG_ERROR, "RTK", "Processing error: %s", e.what());
    }

    const char* status_str;
    switch(ctx->rtk.sol.status) {
        case rtklib::SOLQ_FIX: status_str = "FIXED"; break;
        case rtklib::SOLQ_FLOAT: status_str = "FLOAT"; break;
        case rtklib::SOLQ_SINGLE: status_str = "SINGLE"; break;
        default: status_str = "NO SOLUTION";
    }

    // Convert to Java string
    jstring jstatus = env->NewStringUTF(status_str);

    // Call status callback
    env->CallVoidMethod(thiz, method_onSolutionStatus, jstatus);
    env->DeleteLocalRef(jstatus);

    // Callback position update
    if(ctx->rtk.sol.status == rtklib::SOLQ_FIX) {
        jdouble lat = ctx->rtk.sol.rr[0];
        jdouble lon = ctx->rtk.sol.rr[1];
        jdouble alt = ctx->rtk.sol.rr[2];

        env->CallVoidMethod(thiz, method_updatePosition, lat, lon, alt);
    }
}

// Helper function to convert Android GnssMeasurement to RTKLIB obsd_t
rtklib::obsd_t convert_gnss_measurement(JNIEnv *env, jobject meas_obj) {
    rtklib::obsd_t obs = {0};

    // Get field values from Java object
    jclass cls = env->GetObjectClass(meas_obj);
    obs.sat = static_cast<uint8_t>(env->CallIntMethod(meas_obj,
        env->GetMethodID(cls, "getSvid", "()I")));

    // ... convert other fields (pseudorange, carrier phase, etc)

    return obs;
}