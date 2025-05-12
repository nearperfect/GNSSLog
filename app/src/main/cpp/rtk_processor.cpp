#include "rtk_processor.h"
#include <android/log.h>
#include "rtklib/srtk.h"  // Your RTK library header
#include <cmath>
#include <ctime>

//namespace rtklib = ::rtk;
//EXPORT void rtkinit(rtk_t *rtk, const prcopt_t *opt);

// Temporary navigation data storage
static nav_t nav_default = {0};

static JavaVM *jvm = nullptr;
static jclass class_RTKProcessor;
static jmethodID method_updatePosition;


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
    obs->SNR[0] = static_cast<uint16_t>(cn0DbHz * SNR_UNIT);  // RTKLIB uses 0.001 dB-Hz units

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

    jclass cls = env->FindClass("com/example/geodgnss/RTKProcessor");
    class_RTKProcessor = static_cast<jclass>(env->NewGlobalRef(cls));
    method_updatePosition = env->GetMethodID(cls, "updatePosition", "(DDD)V");

    return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL
Java_com_example_geodgnss_RTKProcessor_initRtkContext(JNIEnv *env, jobject thiz) {
    auto *ctx = new artk_t();
    return reinterpret_cast<jlong>(ctx);
}

// Initialize navigation data (call this once)
JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_initNavigation(JNIEnv *env, jobject thiz) {
}

JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_updateRtcmData(JNIEnv *env, jobject thiz,
                                                      jlong context_handle,
                                                      jbyteArray rtcm_data,
                                                      jlong receiver_time) {
    auto *ctx = reinterpret_cast<artk_t*>(context_handle);

    // Process RTCM data
    jbyte *rtcm_bytes = env->GetByteArrayElements(rtcm_data, nullptr);
    jsize rtcm_len = env->GetArrayLength(rtcm_data);


    char *rtcm_buff = new char [rtcm_len];
    // Process each byte individually
    for (int i = 0; i < rtcm_len; i++) {
        rtcm_buff[i] = rtcm_bytes[i];
    }

    ctx->add_base_buf(rtcm_buff, rtcm_len);

    env->ReleaseByteArrayElements(rtcm_data, rtcm_bytes, JNI_ABORT);

    delete[] rtcm_buff;

}


JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_processRtkData(JNIEnv *env, jobject thiz,
                                                     jlong context_handle,
                                                     jobjectArray measurements,
                                                     jlong receiver_time) {
    auto *ctx = reinterpret_cast<artk_t*>(context_handle);


    // Process GNSS measurements
    jsize num_meas = env->GetArrayLength(measurements);
    obsd_t *obs = new obsd_t[num_meas];

    for (jsize i = 0; i < num_meas; i++) {
        jobject meas_obj = env->GetObjectArrayElement(measurements, i);
        // Convert Android GnssMeasurement to RTKLIB obsd_t
        convert_gnss_measurement(env, meas_obj, &obs[i]);
        env->DeleteLocalRef(meas_obj);
    }

	double pos[3] = { 0 };

	ctx->add_rove_obs(obs, num_meas, pos);

	char gga[255] = { 0 };
	//if (ctx->proc(gga))
	{
		/* output to status
        // Callback position update
        if(ctx->rtk.sol.stat == SOLQ_FIX) {
            jdouble lat = ctx->rtk.sol.rr[0];
            jdouble lon = ctx->rtk.sol.rr[1];
            jdouble alt = ctx->rtk.sol.rr[2];
            env->CallVoidMethod(thiz, method_updatePosition, lat, lon, alt);
        }*/
	}

    delete[] obs;

}

JNIEXPORT void JNICALL
Java_com_example_geodgnss_RTKProcessor_shutdownRtkContext(JNIEnv *env, jobject thiz,
                                                          jlong context_handle) {
}
