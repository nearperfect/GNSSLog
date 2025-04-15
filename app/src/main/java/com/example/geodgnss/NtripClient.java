package com.example.geodgnss;

import android.util.Base64;
import android.util.Log;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.Socket;
import java.util.Arrays;


public class NtripClient {
    private static final String TAG = "NtripClient";
    private Socket socket;
    private BufferedWriter writer;
    private BufferedReader reader;
    private Thread connectionThread;
    private boolean isRunning = false;

    // NTRIP parameters
    private String serverIp;
    private int serverPort;
    private String mountPoint;
    private String username;
    private String password;

    // Add this constant
    public static final int GGA_INTERVAL = 30000; // 30 seconds



    public interface NtripCallback {
        void onRtcmDataReceived(byte[] data);
        void onConnectionStatusChanged(boolean connected);
        void onError(String message);
    }

    public NtripClient(String serverIp, int serverPort, String mountPoint,
                       String username, String password) {
        this.serverIp = serverIp;
        this.serverPort = serverPort;
        this.mountPoint = mountPoint;
        this.username = username;
        this.password = password;
    }

    public void connect(NtripCallback callback) {
        if (isRunning) return;

        connectionThread = new Thread(() -> {
            try {
                socket = new Socket(serverIp, serverPort);
                writer = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));
                reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));

                // Send NTRIP request
                String auth = Base64.encodeToString((username + ":" + password).getBytes(), Base64.NO_WRAP);
                String request = "GET /" + mountPoint + " HTTP/1.1\r\n" +
                        "User-Agent: NTRIP AndroidClient\r\n" +
                        "Authorization: Basic " + auth + "\r\n\r\n";
                writer.write(request);
                writer.flush();

                // Read response
                String response = reader.readLine();
                if (response != null && response.contains("200")) {
                    isRunning = true;
                    callback.onConnectionStatusChanged(true);
                    readRtcmData(callback);
                } else {
                    callback.onError("Connection failed: " + response);
                }
            } catch (IOException e) {
                callback.onError("Connection error: " + e.getMessage());
            }
        });
        connectionThread.start();
    }

    private void readRtcmData(NtripCallback callback) {
        try {
            InputStream input = socket.getInputStream();
            byte[] buffer = new byte[4096];
            int bytesRead;

            while (isRunning && (bytesRead = input.read(buffer)) != -1) {
                byte[] rtcmData = Arrays.copyOf(buffer, bytesRead);
                callback.onRtcmDataReceived(rtcmData);
            }
        } catch (IOException e) {
            callback.onError("Data read error: " + e.getMessage());
        } finally {
            disconnect();
        }
    }

    // Modify sendGga method
    public void sendGga(String ggaSentence) {
        if (writer != null && isRunning && !ggaSentence.isEmpty()) {
            // Execute network operations in background thread
            new Thread(() -> {
                try {
                    String nmea = ggaSentence + "\r\n";
                    writer.write(nmea);
                    writer.flush();
                    Log.d(TAG, "Sent GGA: " + nmea.trim());
                } catch (IOException e) {
                    Log.e(TAG, "Error sending GGA: " + e.getMessage());
                }
            }).start();
        }
    }
    public void disconnect() {
        isRunning = false;
        try {
            if (writer != null) writer.close();
            if (reader != null) reader.close();
            if (socket != null) socket.close();
        } catch (IOException e) {
            Log.e(TAG, "Disconnect error: " + e.getMessage());
        }
    }
}
