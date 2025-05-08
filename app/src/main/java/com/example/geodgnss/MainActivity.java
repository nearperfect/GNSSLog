package com.example.geodgnss;

// At top of MainActivity.java

import static com.example.geodgnss.NtripClient.GGA_INTERVAL;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.location.GnssClock;
import android.location.GnssMeasurement;
import android.location.GnssMeasurementsEvent;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;

import com.google.android.material.snackbar.Snackbar;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.widget.NestedScrollView;
import androidx.navigation.NavController;
import androidx.navigation.Navigation;
import androidx.navigation.ui.AppBarConfiguration;
import androidx.navigation.ui.NavigationUI;

import com.example.geodgnss.databinding.ActivityMainBinding;

import android.view.Menu;
import android.view.MenuItem;
import android.widget.ScrollView;
import android.widget.TextView;

import java.text.DecimalFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity
        implements RTKProcessor.RtkResultListener, NtripClient.NtripCallback {
    private NtripClient ntripClient;
    private LocationManager locationManager;
    private boolean isLocationUpdatesRequested = false;

    private TextView logTextView;
    private TextView ntripTextView;
    private StringBuilder gnssLogBuilder = new StringBuilder();
    private StringBuilder ntripLogBuilder = new StringBuilder();

    private Handler ggaHandler = new Handler();
    private Runnable ggaRunnable;
    private Location lastLocation;
    private int satelliteCount;

    // Add these declarations
    private View statusIndicator;
    private List<GnssMeasurement> currentMeasurements = new ArrayList<>();

    //rtk processor
    private RTKProcessor rtkProcessor;
    private long rtkContextHandle;

    private GnssMeasurementsEvent.Callback gnssCallback = new GnssMeasurementsEvent.Callback() {
        @Override
        public void onGnssMeasurementsReceived(GnssMeasurementsEvent event) {
            synchronized (currentMeasurements) {
                currentMeasurements.clear();
                currentMeasurements.addAll(event.getMeasurements());
            }
            processMeasurements(event);
        }

        @Override
        public void onStatusChanged(int status) {
            // Handle status changes
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        logTextView = findViewById(R.id.logTextView);
        ntripTextView = findViewById(R.id.ntripTextView);
        statusIndicator = findViewById(R.id.statusIndicator);

        rtkProcessor = new RTKProcessor();
        rtkProcessor.setResultListener(this);
        rtkContextHandle = rtkProcessor.initRtkContext();
        rtkProcessor.initNavigation();


        // Initialize NTRIP client
        ntripClient = new NtripClient("rtk.geodnet.com", 2101, "AUTO", "davidchen", "geodnet2024");
//        ntripClient = new NtripClient("120.253.226.97", 8001, "RTCM33_GRCEJ", "cuke004", "4h5s68ja");

        // Initialize location manager
        locationManager = (LocationManager) getSystemService(LOCATION_SERVICE);
        requestLocationUpdates();
    }

    @Override
    protected void onDestroy() {
        rtkProcessor.shutdownRtkContext(rtkContextHandle);
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (checkPermission()) {
            startGnssListening();
            requestLocationUpdates();  // Re-request location updates
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        stopGnssListening();
    }

    private boolean checkPermission() {
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
            return true;
        }
        ActivityCompat.requestPermissions(this,
                new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, 1);
        return false;
    }

    // Add permission result handling
    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 1) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                startGnssListening();
                requestLocationUpdates();
            } else {
                ntripLogBuilder.append("Location permission denied\n");
                updateNtripLog();
            }
        }
    }

    @Override
    public void onPositionUpdate(double lat, double lon, double alt) {
        runOnUiThread(() -> {
            gnssLogBuilder.append(String.format(Locale.US,
                    "RTK Fix: %.8f, %.8f, %.2f\n", lat, lon, alt));
            updateGnssLog();
        });
    }

    @Override
    public void onSolutionStatus(String status) {
        runOnUiThread(() -> {
            String statusMessage = "RTK Status: " + status + "\n";
            gnssLogBuilder.append(statusMessage);

            // Update status indicator
            switch(status) {
                case "FIXED":
                    statusIndicator.setBackgroundColor(Color.GREEN);
                    break;
                case "FLOAT":
                    statusIndicator.setBackgroundColor(Color.YELLOW);
                    break;
                default:
                    statusIndicator.setBackgroundColor(Color.RED);
            }

            updateGnssLog();
        });
    }

    private void startGnssListening() {
        if (locationManager != null) {
            locationManager.registerGnssMeasurementsCallback(gnssCallback);
            if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                return;
            }

        }
    }

    private void stopGnssListening() {
        if (locationManager != null) {
            locationManager.unregisterGnssMeasurementsCallback(gnssCallback);
        }
    }

    private void processMeasurements(GnssMeasurementsEvent event) {
        runOnUiThread(() -> {
            GnssClock clock = event.getClock();
          for (GnssMeasurement measurement : event.getMeasurements()) {

                String clockStream =
                        String.format(
                                "Raw,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
                                SystemClock.elapsedRealtime(),
                                clock.getTimeNanos(),
                                clock.hasLeapSecond() ? clock.getLeapSecond() : "",
                                clock.hasTimeUncertaintyNanos() ? clock.getTimeUncertaintyNanos() : "",
                                clock.getFullBiasNanos(),
                                clock.hasBiasNanos() ? clock.getBiasNanos() : "",
                                clock.hasBiasUncertaintyNanos() ? clock.getBiasUncertaintyNanos() : "",
                                clock.hasDriftNanosPerSecond() ? clock.getDriftNanosPerSecond() : "",
                                clock.hasDriftUncertaintyNanosPerSecond()
                                        ? clock.getDriftUncertaintyNanosPerSecond()
                                        : "",
                                clock.getHardwareClockDiscontinuityCount() + ",");

                //gnssLogBuilder.append(clockStream).append("\n");
                String measurementStream =
                        String.format(
                                "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
                                measurement.getSvid(),
                                measurement.getTimeOffsetNanos(),
                                measurement.getState(),
                                measurement.getReceivedSvTimeNanos(),
                                measurement.getReceivedSvTimeUncertaintyNanos(),
                                measurement.getCn0DbHz(),
                                measurement.getPseudorangeRateMetersPerSecond(),
                                measurement.getPseudorangeRateUncertaintyMetersPerSecond(),
                                measurement.getAccumulatedDeltaRangeState(),
                                measurement.getAccumulatedDeltaRangeMeters(),
                                measurement.getAccumulatedDeltaRangeUncertaintyMeters(),
                                measurement.hasCarrierFrequencyHz() ? measurement.getCarrierFrequencyHz() : "",
                                measurement.hasCarrierCycles() ? measurement.getCarrierCycles() : "",
                                measurement.hasCarrierPhase() ? measurement.getCarrierPhase() : "",
                                measurement.hasCarrierPhaseUncertainty()
                                        ? measurement.getCarrierPhaseUncertainty()
                                        : "",
                                measurement.getMultipathIndicator(),
                                measurement.hasSnrInDb() ? measurement.getSnrInDb() : "",
                                measurement.getConstellationType(),
                                Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                                        && measurement.hasAutomaticGainControlLevelDb()
                                        ? measurement.getAutomaticGainControlLevelDb()
                                        : "");

              gnssLogBuilder.append(measurementStream).append("\n\n");

              updateGnssLog();

          }
        });
    }

    private ExecutorService rtkExecutor = Executors.newSingleThreadExecutor();

    private void processRtcmData(byte[] rtcmData) {
        rtkExecutor.execute(() -> {
            GnssMeasurement[] measurements = getCurrentMeasurements();
            rtkProcessor.processRtkData(rtkContextHandle, rtcmData, measurements,
                    System.currentTimeMillis());
        });
    }

    // Add to locationListener
    private final LocationListener locationListener = new LocationListener() {
        private boolean bLocationUpdated = false;
        @Override
        public void onLocationChanged(Location location) {
            lastLocation = location;
            Log.e("LocationManager", "" + location);
            if(!bLocationUpdated) {
                ntripLogBuilder.append("Location updated: "
                        + location.getLatitude() + ", "
                        + location.getLongitude() + "\n");
                updateNtripLog();
                bLocationUpdated = true;
            }
        }

        @Override
        public void onStatusChanged(String provider, int status, Bundle extras) {
            // Handle status changes
        }

        @Override
        public void onProviderEnabled(String provider) {
            ntripLogBuilder.append("GPS enabled\n");
            updateNtripLog();
        }

        @Override
        public void onProviderDisabled(String provider) {
            ntripLogBuilder.append("GPS disabled\n");
            updateNtripLog();
        }
    };

    private void requestLocationUpdates() {
        if (checkPermission() && !isLocationUpdatesRequested) {
            try {
                // Request both GNSS measurements and location updates
                if (locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                    locationManager.requestLocationUpdates(
                            LocationManager.GPS_PROVIDER,
                            500L,
                            0f,
                            locationListener
                    );
                }

                if (locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                    locationManager.requestLocationUpdates(
                            LocationManager.NETWORK_PROVIDER,
                            500L,
                            0f,
                            locationListener
                    );
                }

                isLocationUpdatesRequested = true;
                ntripLogBuilder.append("Started location updates\n");
                updateNtripLog();

            } catch (SecurityException e) {
                ntripLogBuilder.append("Location permission denied\n");
                updateNtripLog();
            }
        }
    }

    private void updateGnssLog() {
        // Limit log size
        if (gnssLogBuilder.length() > 3000) {
            gnssLogBuilder.delete(0, 1500);
        }

        logTextView.setText(gnssLogBuilder.toString());
        // Correct scrolling implementation
        final NestedScrollView scrollView = (NestedScrollView) logTextView.getParent();
        scrollView.post(() -> scrollView.fullScroll(View.FOCUS_DOWN));
    }

    private void updateNtripLog() {
        // Limit log size
        if (ntripLogBuilder.length() > 3000) {
            ntripLogBuilder.delete(0, 1500);
        }
        ntripTextView.setText(ntripLogBuilder.toString());
        // Correct scrolling implementation
        final NestedScrollView scrollView = (NestedScrollView) ntripTextView.getParent();
        scrollView.post(() -> scrollView.fullScroll(View.FOCUS_DOWN));
    }

    @Override
    public void onRtcmDataReceived(byte[] data) {
        runOnUiThread(() -> {
            // Process RTCM data
            String message = "Received RTCM: " + data.length + " bytes\n";
            ntripLogBuilder.append(message);
            updateNtripLog();

            processRtcmData(data);
        });
    }

    @Override
    public void onConnectionStatusChanged(boolean connected) {
        runOnUiThread(() -> {
            String status = connected ? "Connected to NTRIP" : "Disconnected";
            ntripLogBuilder.append(status).append("\n");
            updateNtripLog();

            if (connected) {
                startGgaUpdates();
            } else {
                stopGgaUpdates();
            }

        });
    }

    @Override
    public void onError(String message) {
        runOnUiThread(() -> {
            ntripLogBuilder.append("Error: ").append(message).append("\n");
            updateNtripLog();
        });
    }

    // Add connection controls
    public void onConnectClick(View view) {
        ntripClient.connect(this);
    }

    public void onDisconnectClick(View view) {
        ntripClient.disconnect();
    }

    private GnssMeasurement[] getCurrentMeasurements() {
        synchronized (currentMeasurements) {
            return currentMeasurements.toArray(new GnssMeasurement[0]);
        }
    }
    private String generateGGA(GnssMeasurement measurement, Location location) {
        // Convert time to NMEA format
        SimpleDateFormat sdf = new SimpleDateFormat("HHmmss.SSS", Locale.getDefault());
        String time = sdf.format(new Date(measurement.getReceivedSvTimeNanos() / 1000000));

        // Convert coordinates to NMEA format
        double lat = location.getLatitude();
        double lon = location.getLongitude();
        String nsew = lat >= 0 ? "N" : "S";
        String ew = lon >= 0 ? "E" : "W";

        String latStr = convertToNmeaFormat(Math.abs(lat), true);
        String lonStr = convertToNmeaFormat(Math.abs(lon), false);

        // Construct GGA sentence
        String gga = String.format(Locale.US, "$GPGGA,%s,%s,%s,%s,%s,1,%02d,%.1f,%.1f,M,,M,,",
                time, latStr, nsew, lonStr, ew,
                measurement.getSvid(),
                location.getAccuracy(),
                location.getAltitude());

        // Calculate checksum
        int checksum = 0;
        for (int i = 1; i < gga.length(); i++) {
            checksum ^= gga.charAt(i);
        }

        return String.format("*%02X\r\n", checksum);
    }

    private String convertToNmeaFormat(double decimalDegrees, boolean isLatitude) {
        int degrees = (int) decimalDegrees;
        double minutes = (decimalDegrees - degrees) * 60;
        return String.format(Locale.US, "%02d%07.4f", degrees, minutes);
    }


    // Add GGA transmission methods
    private void startGgaUpdates() {
        ggaRunnable = new Runnable() {
            @Override
            public void run() {
                sendGgaToServer();
                ggaHandler.postDelayed(this, GGA_INTERVAL);
            }
        };
        ggaHandler.postDelayed(ggaRunnable, GGA_INTERVAL);
    }

    private void stopGgaUpdates() {
        ggaHandler.removeCallbacks(ggaRunnable);
    }

    private void sendGgaToServer() {
        // Try to get last known location if not available
        if (lastLocation == null && checkPermission()) {
            lastLocation = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
        }

        if (lastLocation != null) {
            String gga = generateGGA(lastLocation, satelliteCount);
            ntripClient.sendGga(gga);

            ntripLogBuilder.append("Sent GGA: ").append(gga).append("\n");
            updateNtripLog();
        } else {
            ntripLogBuilder.append("No location available for GGA\n");
            updateNtripLog();
        }
    }


    // Updated GGA generator
    private String generateGGA(Location location, int satelliteCount) {
        SimpleDateFormat sdf = new SimpleDateFormat("HHmmss.SSS", Locale.US);
        sdf.setTimeZone(TimeZone.getTimeZone("UTC"));
        String time = sdf.format(new Date(location.getTime()));

        DecimalFormat df = new DecimalFormat("00.000000");
        String lat = df.format(Math.abs(location.getLatitude()));
        String ns = location.getLatitude() >= 0 ? "N" : "S";
        String lon = df.format(Math.abs(location.getLongitude()));
        String ew = location.getLongitude() >= 0 ? "E" : "W";

        return String.format(Locale.US, "$GPGGA,%s,%s,%s,%s,%s,1,%02d,%.1f,%.1f,M,,M,,",
                time,
                lat.substring(0, 2) + lat.substring(3), // Degrees + decimal minutes
                ns,
                lon.substring(0, 3) + lon.substring(4), // Degrees + decimal minutes
                ew,
                satelliteCount,
                location.getAccuracy(),
                location.getAltitude());
    }
}