package com.example.geodgnss;

import android.location.GnssMeasurement;
import android.os.Handler;
import android.os.Looper;

public class RTKProcessor {


    static {
        System.loadLibrary("rtkprocessor");
    }

    public interface RtkResultListener {
        void onPositionUpdate(double lat, double lon, double alt);
        void onSolutionStatus(String status);
    }

    private RtkResultListener listener;

    public native void initNavigation();
    public native long initRtkContext();

    public native void updateRtcmData(long contextHandle, byte[] rtcmData,
                                      long receiverTime);
    public native void processRtkData(long contextHandle,
                                      GnssMeasurement[] measurements,
                                      long receiverTime);
    public native void shutdownRtkContext(long contextHandle);

    public void setResultListener(RtkResultListener listener) {
        this.listener = listener;
    }

    // Called from native code
    @SuppressWarnings("unused")
    private void updatePosition(double lat, double lon, double alt) {
        if (listener != null) {
            new Handler(Looper.getMainLooper()).post(() ->
                    listener.onPositionUpdate(lat, lon, alt));
        }
    }

    // Called from native code
    @SuppressWarnings("unused")
    private void solutionStatus(String str) {
        if (listener != null) {
            new Handler(Looper.getMainLooper()).post(() ->
                    listener.onSolutionStatus(str));
        }
    }
}
