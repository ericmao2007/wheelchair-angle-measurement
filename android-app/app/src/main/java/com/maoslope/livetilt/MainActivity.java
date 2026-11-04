package com.maoslope.livetilt;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import org.json.JSONObject;
import java.io.ByteArrayInputStream;
import java.io.IOException;

public final class MainActivity extends Activity implements BleClient.Listener {
    private WebView web;
    private BleClient ble;
    private boolean pageReady;
    private boolean foreground;
    private boolean pendingConnect;
    private static final String ORIGIN = "https://appassets.androidplatform.net/assets/";
    private static final int PERMISSION_REQUEST = 41;
    private static final int BLUETOOTH_REQUEST = 42;
    private static final int LOCATION_REQUEST = 43;
    private LocationManager locations;
    private boolean locationWanted;
    private boolean locationPermissionPending;
    private final LocationListener locationListener = new LocationListener() {
        @Override public void onLocationChanged(Location l) {
            if (!foreground || !locationWanted || !l.hasAccuracy()) return;
            try {
                double age = Math.max(0, (SystemClock.elapsedRealtimeNanos() - l.getElapsedRealtimeNanos()) / 1e6);
                JSONObject d = new JSONObject().put("latitude", l.getLatitude()).put("longitude", l.getLongitude())
                        .put("accuracy", l.getAccuracy()).put("ageMs", age).put("timestamp", l.getTime());
                evaluate("window.onNativeLocation(" + d + ");");
            } catch (org.json.JSONException ignored) { }
        }
        @Override public void onProviderEnabled(String provider) { if (locationWanted) startLocation(); }
        @Override public void onProviderDisabled(String provider) {
            locationStatus("Location provider disabled. Enable phone Location and retry.");
        }
        @Override public void onStatusChanged(String provider, int status, Bundle extras) { }
    };
    private String state = "idle", message = "", metadata = "";

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(11,18,32));
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(android.view.WindowInsets.Type.systemBars()
                        | android.view.WindowInsets.Type.displayCutout());
                v.setPadding(i.left, i.top, i.right, i.bottom);
            } else {
                v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(11,18,32));
        web.getSettings().setJavaScriptEnabled(true);
        web.getSettings().setUserAgentString(web.getSettings().getUserAgentString() + " LiveTilt/1.1");
        web.getSettings().setAllowFileAccess(false);
        web.getSettings().setAllowContentAccess(false);
        web.getSettings().setJavaScriptCanOpenWindowsAutomatically(false);
        web.getSettings().setMediaPlaybackRequiresUserGesture(true);
        web.addJavascriptInterface(new Bridge(), "LiveTiltAndroid");
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                if (r.isForMainFrame() && r.hasGesture()
                        && r.getUrl().toString().equals("https://www.openstreetmap.org/copyright")) {
                    try { startActivity(new Intent(Intent.ACTION_VIEW, r.getUrl())); }
                    catch (android.content.ActivityNotFoundException ignored) { }
                }
                return true;
            }
            @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) {
                String url = r.getUrl().toString();
                String path = url.startsWith(ORIGIN) ? url.substring(ORIGIN.length()) : "";
                if (path.equals("index.html") || path.equals("protocol.js") || path.equals("tracking.js") || path.equals("map.js")) {
                    try { return new WebResourceResponse(path.endsWith(".js") ? "application/javascript" : "text/html",
                            "UTF-8", getAssets().open(path)); }
                    catch (IOException ignored) { }
                }
                // Only map images may leave the packaged UI; remote scripts/pages stay blocked.
                if (url.matches("https://tile\\.openstreetmap\\.org/[0-9]+/[0-9]+/[0-9]+\\.png")) return null;
                return new WebResourceResponse("text/plain", "UTF-8", 404, "Not Found", null,
                        new ByteArrayInputStream(new byte[0]));
            }
        });
        root.addView(web, new FrameLayout.LayoutParams(-1,-1));
        setContentView(root);
        ble = new BleClient(this, this);
        locations = (LocationManager) getSystemService(LOCATION_SERVICE);
        web.loadUrl(ORIGIN + "index.html");
    }

    private void connectWithPermission() {
        if (!foreground) return;
        if (ble.isWanted()) { ble.disconnect(); return; }
        String[] needed = Build.VERSION.SDK_INT >= 31
                ? new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT}
                : new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        for (String p : needed) {
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(needed, PERMISSION_REQUEST); return;
            }
        }
        if (!ble.supported()) { onStatus("error", "Bluetooth LE is not available on this phone.", ""); return; }
        if (!ble.enabled()) {
            startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), BLUETOOTH_REQUEST); return;
        }
        ble.connect();
    }

    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == LOCATION_REQUEST) {
            locationPermissionPending = false;
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
                if (foreground && locationWanted) startLocation();
            } else {
                locationWanted = false;
                locationStatus("Precise location is required to record map points. Enable it in app settings, then retry GPS.");
            }
            return;
        }
        if (request != PERMISSION_REQUEST) return;
        boolean granted = results.length > 0;
        for (int result : results) granted &= result == PackageManager.PERMISSION_GRANTED;
        if (granted) { pendingConnect = true; if (foreground) { pendingConnect = false; connectWithPermission(); } }
        else onStatus("error", "Bluetooth permission is required. Allow Nearby devices in app settings and retry.", "");
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == BLUETOOTH_REQUEST) {
            if (result == RESULT_OK) { pendingConnect = true; if (foreground) { pendingConnect = false; connectWithPermission(); } }
            else onStatus("idle", "Turn on Bluetooth to connect.", "");
        }
    }
    @Override protected void onResume() {
        super.onResume(); foreground = true;
        if (web != null) web.onResume();
        if (ble != null) ble.resume();
        if (locationWanted) startLocation();
        if (pendingConnect) { pendingConnect = false; connectWithPermission(); }
    }
    @Override protected void onStop() {
        foreground = false;
        if (locations != null) locations.removeUpdates(locationListener);
        locationStatus("Paused in background");
        if (ble != null) ble.pause();
        if (web != null) web.onPause();
        super.onStop();
    }
    @Override protected void onDestroy() {
        pageReady = false;
        if (ble != null) ble.disconnect();
        if (web != null) { web.removeJavascriptInterface("LiveTiltAndroid"); web.destroy(); web = null; }
        super.onDestroy();
    }
    @Override public void onStatus(String nextState, String nextMessage, String nextMetadata) {
        state = nextState; message = nextMessage; metadata = nextMetadata;
        publishState();
        getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (state.equals("ready")) getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    private void publishState() {
        try {
            JSONObject data = new JSONObject().put("state", state).put("message", message).put("metadata", metadata);
            evaluate("window.onNativeState(" + data + ");");
        } catch (org.json.JSONException ignored) { }
    }
    @Override public void onFrame(byte[] payload) {
        String base64 = android.util.Base64.encodeToString(payload, android.util.Base64.NO_WRAP);
        evaluate("window.onNativeFrame(" + JSONObject.quote(base64) + ");");
    }
    @Override public void onNotice(String text) { evaluate("window.onNativeNotice(" + JSONObject.quote(text) + ");"); }
    private void evaluate(String script) { if (pageReady && web != null) web.evaluateJavascript(script, null); }

    private void locationStatus(String text) {
        evaluate("window.onNativeLocationState(" + JSONObject.quote(text) + ");");
    }
    private void startLocation() {
        if (!foreground || !locationWanted) return;
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            locationStatus("Allow precise location while using the app.");
            if (locationPermissionPending) return;
            locationPermissionPending = true;
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION}, LOCATION_REQUEST);
            return;
        }
        locations.removeUpdates(locationListener);
        boolean started = false;
        try {
            for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                if (locations.isProviderEnabled(provider)) {
                    locations.requestLocationUpdates(provider, 1000, 0, locationListener);
                    started = true;
                }
            }
            locationStatus(started ? "Waiting for a fresh GPS fix…" : "Enable phone Location, then retry GPS.");
        } catch (SecurityException | IllegalArgumentException e) {
            locationStatus("Location unavailable. Check precise location permission and retry GPS.");
        }
    }

    private final class Bridge {
        @JavascriptInterface public void ready() { runOnUiThread(() -> { pageReady = true; publishState(); }); }
        @JavascriptInterface public void connect() { runOnUiThread(MainActivity.this::connectWithPermission); }
        @JavascriptInterface public void send(String command) { runOnUiThread(() -> ble.send(command)); }
        @JavascriptInterface public void location(boolean enabled) {
            runOnUiThread(() -> {
                locationWanted = enabled;
                if (enabled) startLocation();
                else { locations.removeUpdates(locationListener); locationStatus("GPS recording off"); }
            });
        }
        @JavascriptInterface public String loadRecords() {
            return getSharedPreferences("gradient-map", MODE_PRIVATE).getString("records", "[]");
        }
        @JavascriptInterface public boolean saveRecords(String json) {
            try {
                org.json.JSONArray records = new org.json.JSONArray(json);
                if (records.length() > 10000 || json.length() > 8000000) return false;
                return getSharedPreferences("gradient-map", MODE_PRIVATE).edit().putString("records", json).commit();
            } catch (org.json.JSONException e) { return false; }
        }
    }
}
