#include "rtk_processor.h"
#include <android/log.h>
#include "rtklib/rtklib.h"  // Your RTK library header
#include <cmath>
#include <ctime>

#define TAG "NativeLog"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

//namespace rtklib = ::rtk;
//EXPORT void rtkinit(rtk_t *rtk, const prcopt_t *opt);

// Temporary navigation data storage
static nav_t nav_default = {0};

static JavaVM *jvm = nullptr;
static jclass class_RTKProcessor;
static jmethodID method_updatePosition;
static jmethodID method_solutionStatus;


static void convert_gnss_measurement(JNIEnv *env, jobject meas_obj, obsd_t *obs) {
    jclass cls = env->GetObjectClass(meas_obj);

    // Clear the observation structure
    memset(obs, 0, sizeof(obsd_t));

    // Get basic fields
    jmethodID getSvid = env->GetMethodID(cls, "getSvid", "()I");
    jmethodID getConstellation = env->GetMethodID(cls, "getConstellationType", "()I");
    jmethodID getTimeNanos = env->GetMethodID(cls, "getReceivedSvTimeNanos", "()J");
    jmethodID getCn0DbHz = env->GetMethodID(cls, "getCn0DbHz", "()D");
    jmethodID getPseudorange = env->GetMethodID(cls, "getPseudorangeRateMetersPerSecond", "()D");
    jmethodID getCarrierPhase = env->GetMethodID(cls, "getAccumulatedDeltaRangeMeters", "()D");
    jmethodID getCarrierFrequency = env->GetMethodID(cls, "getCarrierFrequencyHz", "()F");

    // Get Android measurement values
    int svid = env->CallIntMethod(meas_obj, getSvid);
    int constellation = env->CallIntMethod(meas_obj, getConstellation);
    long receivedTimeNanos = env->CallLongMethod(meas_obj, getTimeNanos);
    double cn0DbHz = env->CallDoubleMethod(meas_obj, getCn0DbHz);
    double pseudorange = env->CallDoubleMethod(meas_obj, getPseudorange);
    double carrierPhaseMeters = env->CallDoubleMethod(meas_obj, getCarrierPhase);
    float carrierFrequencyHz = env->CallFloatMethod(meas_obj, getCarrierFrequency);

    // Convert constellation type to RTKLIB system
    int sys = SYS_NONE;
    switch(constellation) {
        case 1:  sys = SYS_GPS; break;  // GnssConstellationType.GPS
        case 3:  sys = SYS_GLO; break;  // GnssConstellationType.GLONASS
        case 5:  sys = SYS_GAL; break;  // GnssConstellationType.GALILEO
        case 4:  sys = SYS_QZS; break;  // GnssConstellationType.QZSS
        case 6:  sys = SYS_CMP; break;  // GnssConstellationType.BEIDOU
        case 2:  sys = SYS_SBS; break;  // GnssConstellationType.SBAS
        default: sys = SYS_NONE; break;
    }

    // Create satellite ID
    obs->sat = satno(sys, svid);
    if (obs->sat == 0) {
        __android_log_print(ANDROID_LOG_ERROR, "RTK", "Invalid satellite: sys=%d prn=%d", sys, svid);
        return;
    }

    // Convert time (Android nanoseconds to GPS time)
    const double gpsepoch[] = {1980, 1, 6, 0, 0, 0};
    gtime_t gpst0 = epoch2time(gpsepoch);

    // Convert received time to seconds since GPS epoch
    double sec = receivedTimeNanos * 1e-9;
    obs->time = timeadd(gpst0, sec);


    // Convert pseudorange and carrier phase
    obs->P[0] = pseudorange;

    // Convert carrier phase meters to cycles
    if (carrierFrequencyHz > 0) {
        double lambda = CLIGHT / carrierFrequencyHz;
        obs->L[0] = carrierPhaseMeters / lambda;
        obs->LLI[0] = 0;
    }

    // Signal strength (convert dB-Hz to SNR unit)
    obs->SNR[0] = static_cast<uint16_t>(cn0DbHz * 1000);  // RTKLIB uses 0.001 dB-Hz units

    // Set default values for other fields
    obs->rcv = 1;  // Receiver number
    obs->code[0] = CODE_L1C;  // Default to L1 C/A code

    __android_log_print(ANDROID_LOG_DEBUG, "RTK", "satellite: sys=%d prn=%d", sys, svid);

}


JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    jvm = vm;
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    LOGI("Android load so init updatePosition call back");
    jclass cls = env->FindClass("com/example/geodgnss/RTKProcessor");
    class_RTKProcessor = static_cast<jclass>(env->NewGlobalRef(cls));
    method_updatePosition = env->GetMethodID(cls, "updatePosition", "(DDD)V");
    //method_solutionStatus = env->GetMethodID(cls, "solutionStatus", "(L)V");
    return JNI_VERSION_1_6;
}

struct RtkContext {
    rtk_t rtk;
    rtcm_t rtcm;        // RTCM control structure
    nav_t nav;          // Navigation data
    obs_t obs;          // Observation data

    // Add destructor for proper cleanup
    ~RtkContext() {
        // Additional cleanup if needed
    }
};

JNIEXPORT jlong JNICALL
Java_com_example_geodgnss_RTKProcessor_initRtkContext(JNIEnv *env, jobject thiz) {
    auto *ctx = new RtkContext();
    // Initialize structures
    memset(&ctx->rtk, 0, sizeof(rtk_t));
    memset(&ctx->nav, 0, sizeof(nav_t));
    memset(&ctx->obs, 0, sizeof(obs_t));

    // Initialize RTKLIB context
    init_rtcm(&ctx->rtcm);
    LOGI("Android load so rtcm init");
    rtkinit(&ctx->rtk, &prcopt_default);
    LOGI("Android load so rtk init");
    return reinterpret_cast<jlong>(ctx);
}

// Initialize navigation data (call this once)
JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_initNavigation(JNIEnv *env, jobject thiz) {
    memset(&nav_default, 0, sizeof(nav_t));
    nav_default.n = nav_default.nmax = 0;
    nav_default.eph = nullptr;
    nav_default.geph = nullptr;
    nav_default.seph = nullptr;
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

    // Process each byte individually
    for (int i = 0; i < rtcm_len; i++) {
        int ret = input_rtcm3(&ctx->rtcm, static_cast<uint8_t>(rtcm_bytes[i]));

        if (ret == 1) {  // Complete message received
            // Update navigation data from RTCM
            if (ctx->rtcm.nav.eph) {
                ctx->nav = ctx->rtcm.nav;
            }
            // Reset RTCM parser for next message
            init_rtcm(&ctx->rtcm);
        }
        else if (ret < 0) {  // Error in message
            __android_log_write(ANDROID_LOG_ERROR, "RTK", "Invalid RTCM message");
        }
    }

    env->ReleaseByteArrayElements(rtcm_data, rtcm_bytes, JNI_ABORT);

    // Process GNSS measurements
    jsize num_meas = env->GetArrayLength(measurements);
    obsd_t *obs = new obsd_t[num_meas];

    for (jsize i = 0; i < num_meas; i++) {
        jobject meas_obj = env->GetObjectArrayElement(measurements, i);
        // Convert Android GnssMeasurement to RTKLIB obsd_t
        convert_gnss_measurement(env, meas_obj, &obs[i]);
        env->DeleteLocalRef(meas_obj);
    }

    // Execute RTK processing
    rtkpos(&ctx->rtk, obs, num_meas, &nav_default);

    LOGE("Android ctx->rtk.sol.stat =%d",ctx->rtk.sol.stat);
    // Callback position update
    if(ctx->rtk.sol.stat == SOLQ_FIX || ctx->rtk.sol.stat == SOLQ_FLOAT) {
        env->CallVoidMethod(thiz, method_updatePosition,
                            ctx->rtk.sol.rr[0], ctx->rtk.sol.rr[1], ctx->rtk.sol.rr[2]);
    }

    delete[] obs;

/*
    const char* status_str;
    switch(ctx->rtk.sol.stat) {
        case SOLQ_FIX: status_str = "FIXED"; break;
        case SOLQ_FLOAT: status_str = "FLOAT"; break;
        case SOLQ_SINGLE: status_str = "SINGLE"; break;
        default: status_str = "NO SOLUTION";
    }

    // Convert to Java string
    jstring jstatus = env->NewStringUTF(status_str);

    // Call status callback
    env->CallVoidMethod(thiz, method_onSolutionStatus, jstatus);
    env->DeleteLocalRef(jstatus);
*/

    // Callback position update
    if(ctx->rtk.sol.stat == SOLQ_FIX) {
        jdouble lat = ctx->rtk.sol.rr[0];
        jdouble lon = ctx->rtk.sol.rr[1];
        jdouble alt = ctx->rtk.sol.rr[2];

        env->CallVoidMethod(thiz, method_updatePosition, lat, lon, alt);
    }
}

JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_shutdownRtkContext(JNIEnv *env, jobject thiz,
                                                          jlong context_handle) {
    auto *ctx = reinterpret_cast<RtkContext*>(context_handle);
    if (ctx != nullptr) {
        // Free RTKLIB resources
        free_rtcm(&ctx->rtcm);
        rtkfree(&ctx->rtk);
        free(ctx->obs.data);
        free(ctx->nav.eph);
        free(ctx->nav.geph);
        free(ctx->nav.seph);
        delete ctx;
    }
}
