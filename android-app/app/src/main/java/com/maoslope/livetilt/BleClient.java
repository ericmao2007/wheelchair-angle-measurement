package com.maoslope.livetilt;

import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.location.LocationManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.os.SystemClock;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Collections;
import java.util.UUID;

/** Foreground-only BLE central. All GATT operations are serialized on the main thread. */
@SuppressLint("MissingPermission")
public final class BleClient {
    public interface Listener {
        void onStatus(String state, String message, String metadata);
        void onFrame(byte[] payload);
        void onNotice(String text);
    }
    static final UUID SERVICE = UUID.fromString("12345678-1234-1234-1234-1234567890ab");
    static final UUID TELEMETRY = UUID.fromString("abcdefab-1234-5678-1234-abcdefabcdef");
    static final UUID CONTROL = UUID.fromString("abcdefac-1234-5678-1234-abcdefabcdef");
    static final UUID METADATA = UUID.fromString("abcdefad-1234-5678-1234-abcdefabcdef");
    static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private final Context context;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final BluetoothAdapter adapter;
    private final FrameAssembler assembler = new FrameAssembler();
    private final ArrayDeque<String> writes = new ArrayDeque<>();
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic telemetry, control;
    private boolean wanted, active = true, scanning, ready, writing;
    private int retry;
    private long lastFrame;
    private String metadata = "";
    private final Runnable scanTimeout = () -> fail("MAO_SLOPE not found. Keep it nearby and disconnect the computer viewer.", true);
    private final Runnable connectTimeout = () -> fail("Bluetooth connection timed out. Retrying…", true);
    private final Runnable writeTimeout = () -> fail("Device did not acknowledge a command. Reconnecting…", true);
    private final Runnable retryTask = this::startScan;
    private final Runnable heartbeat = new Runnable() {
        @Override public void run() {
            if (!ready || !wanted || !active) return;
            if (SystemClock.elapsedRealtime() - lastFrame > 6000) {
                fail("Sensor data stopped. Reconnecting…", true); return;
            }
            handler.postDelayed(this, 1000);
        }
    };

    public BleClient(Context context, Listener listener) {
        this.context = context.getApplicationContext(); this.listener = listener;
        BluetoothManager manager = this.context.getSystemService(BluetoothManager.class);
        adapter = manager == null ? null : manager.getAdapter();
    }
    public boolean supported() { return adapter != null; }
    public boolean enabled() { try { return adapter != null && adapter.isEnabled(); } catch (SecurityException e) { return false; } }
    public boolean isWanted() { return wanted; }
    public void connect() { wanted = true; retry = 0; startScan(); }
    public void disconnect() {
        wanted = false; cleanup(); listener.onStatus("idle", "", "");
    }
    public void pause() {
        active = false; cleanup(); listener.onStatus("paused", "", "");
    }
    public void resume() { active = true; if (wanted) handler.postDelayed(retryTask, 350); }

    private void startScan() {
        if (!wanted || !active) return;
        cleanup();
        if (!enabled()) { wanted = false; listener.onStatus("error", "Turn on Bluetooth, then tap Connect.", ""); return; }
        if (Build.VERSION.SDK_INT < 31) {
            LocationManager lm = context.getSystemService(LocationManager.class);
            boolean locationOn = false;
            try { locationOn = lm != null && (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)); } catch (RuntimeException ignored) { }
            if (!locationOn) {
                wanted = false;
                listener.onStatus("error", "Android 8–11 requires Location services on for BLE scanning. Enable Location and retry.", ""); return;
            }
        }
        try {
            scanner = adapter.getBluetoothLeScanner();
            if (scanner == null) { fail("Bluetooth scanner unavailable.", false); return; }
            scanning = true;
            listener.onStatus("scanning", "Looking for MAO_SLOPE…", "");
            ScanFilter filter = new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build();
            ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            scanner.startScan(Collections.singletonList(filter), settings, scanCallback);
            handler.postDelayed(scanTimeout, 12000);
        } catch (SecurityException e) { fail("Bluetooth permission was revoked. Allow Nearby devices and retry.", false); }
        catch (RuntimeException e) { fail("Could not start Bluetooth scanning. Retry after turning Bluetooth on.", false); }
    }
    private void stopScan() {
        handler.removeCallbacks(scanTimeout);
        if (scanning && scanner != null) { try { scanner.stopScan(scanCallback); } catch (RuntimeException ignored) { } }
        scanning = false; scanner = null;
    }
    private void cleanup() {
        stopScan(); handler.removeCallbacks(connectTimeout); handler.removeCallbacks(retryTask);
        handler.removeCallbacks(writeTimeout); handler.removeCallbacks(heartbeat);
        ready = false; writing = false; telemetry = null; control = null; metadata = "";
        assembler.reset(); writes.clear();
        BluetoothGatt old = gatt; gatt = null;
        if (old != null) { try { old.disconnect(); } catch (RuntimeException ignored) { } try { old.close(); } catch (RuntimeException ignored) { } }
    }
    private void fail(String message, boolean reconnect) {
        cleanup();
        if (!reconnect) wanted = false;
        listener.onStatus(wanted && active ? "retrying" : "error", message, "");
        if (wanted && active) handler.postDelayed(retryTask, Math.min(15000, 2000L << Math.min(retry++, 3)));
    }
    private final ScanCallback scanCallback = new ScanCallback() {
        @Override public void onScanResult(int type, ScanResult result) {
            handler.post(() -> {
                if (!scanning || !wanted || !active) return;
                String name = result.getScanRecord() == null ? null : result.getScanRecord().getDeviceName();
                try { if (name == null) name = result.getDevice().getName(); } catch (SecurityException ignored) { }
                if (!"MAO_SLOPE".equals(name)) return;
                stopScan(); listener.onStatus("connecting", "Connecting to MAO_SLOPE…", "");
                try {
                    gatt = result.getDevice().connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE);
                    if (gatt == null) { fail("Could not connect to the sensor.", true); return; }
                    handler.postDelayed(connectTimeout, 20000);
                } catch (RuntimeException e) { fail("Bluetooth connection failed.", false); }
            });
        }
        @Override public void onScanFailed(int error) {
            handler.post(() -> { if (scanning) fail("Bluetooth scan failed (" + error + "). Tap Connect to retry.", false); });
        }
    };
    private final BluetoothGattCallback callback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt device, int status, int state) {
            handler.post(() -> {
                if (device != gatt) return;
                if (status == BluetoothGatt.GATT_SUCCESS && state == BluetoothProfile.STATE_CONNECTED) {
                    try { if (!device.discoverServices()) fail("Could not discover device services.", true); }
                    catch (SecurityException e) { fail("Bluetooth permission revoked.", false); }
                } else if (state == BluetoothProfile.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                    fail("Sensor disconnected. Reconnecting…", true);
                }
            });
        }
        @Override public void onServicesDiscovered(BluetoothGatt device, int status) {
            handler.post(() -> {
                if (device != gatt) return;
                BluetoothGattService service = status == BluetoothGatt.GATT_SUCCESS ? device.getService(SERVICE) : null;
                if (service == null) { fail("MAO_SLOPE service is missing.", false); return; }
                telemetry = service.getCharacteristic(TELEMETRY); control = service.getCharacteristic(CONTROL);
                BluetoothGattCharacteristic meta = service.getCharacteristic(METADATA);
                if (telemetry == null || control == null || meta == null
                        || (telemetry.getProperties() & BluetoothGattCharacteristic.PROPERTY_NOTIFY) == 0
                        || (control.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE) == 0) {
                    fail("Unsupported sensor firmware. Install STATIC2 firmware.", false); return;
                }
                try { if (!device.readCharacteristic(meta)) fail("Could not read firmware version.", true); }
                catch (SecurityException e) { fail("Bluetooth permission revoked.", false); }
            });
        }
        @Override public void onCharacteristicRead(BluetoothGatt device, BluetoothGattCharacteristic c, byte[] value, int status) {
            byte[] copy = value.clone(); handler.post(() -> handleMetadata(device, c.getUuid(), copy, status));
        }
        @Override public void onCharacteristicRead(BluetoothGatt device, BluetoothGattCharacteristic c, int status) {
            if (Build.VERSION.SDK_INT < 33) {
                byte[] value = c.getValue(); byte[] copy = value == null ? new byte[0] : value.clone();
                handler.post(() -> handleMetadata(device, c.getUuid(), copy, status));
            }
        }
        @Override public void onDescriptorWrite(BluetoothGatt device, BluetoothGattDescriptor descriptor, int status) {
            handler.post(() -> {
                if (device != gatt || !CCCD.equals(descriptor.getUuid())) return;
                if (status != BluetoothGatt.GATT_SUCCESS) { fail("Could not subscribe to sensor data.", true); return; }
                handler.removeCallbacks(connectTimeout); ready = true; retry = 0;
                lastFrame = SystemClock.elapsedRealtime();
                listener.onStatus("ready", "", metadata); handler.postDelayed(heartbeat, 1000);
            });
        }
        @Override public void onCharacteristicChanged(BluetoothGatt device, BluetoothGattCharacteristic c, byte[] value) {
            byte[] copy = value.clone(); handler.post(() -> handlePacket(device, c.getUuid(), copy));
        }
        @Override public void onCharacteristicChanged(BluetoothGatt device, BluetoothGattCharacteristic c) {
            if (Build.VERSION.SDK_INT < 33) {
                byte[] value = c.getValue(); if (value == null) return;
                byte[] copy = value.clone(); handler.post(() -> handlePacket(device, c.getUuid(), copy));
            }
        }
        @Override public void onCharacteristicWrite(BluetoothGatt device, BluetoothGattCharacteristic c, int status) {
            handler.post(() -> {
                if (device != gatt || !CONTROL.equals(c.getUuid())) return;
                handler.removeCallbacks(writeTimeout); writing = false;
                if (status != BluetoothGatt.GATT_SUCCESS) listener.onNotice("Command failed. Check the connection and retry.");
                nextWrite();
            });
        }
    };
    private void handleMetadata(BluetoothGatt device, UUID uuid, byte[] value, int status) {
        if (device != gatt || !METADATA.equals(uuid)) return;
        metadata = new String(value, StandardCharsets.US_ASCII).trim();
        if (status != BluetoothGatt.GATT_SUCCESS || !metadata.startsWith("STATIC2:")) {
            fail("Unsupported sensor firmware. Install STATIC2 firmware.", false); return;
        }
        try {
            BluetoothGattDescriptor descriptor = telemetry.getDescriptor(CCCD);
            if (descriptor == null || !device.setCharacteristicNotification(telemetry, true)) {
                fail("Sensor notifications are unavailable.", false); return;
            }
            boolean started;
            if (Build.VERSION.SDK_INT >= 33) started = device.writeDescriptor(descriptor,
                    BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS;
            else { descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE); started = device.writeDescriptor(descriptor); }
            if (!started) fail("Could not enable sensor notifications.", true);
        } catch (SecurityException e) { fail("Bluetooth permission revoked.", false); }
    }
    private void handlePacket(BluetoothGatt device, UUID uuid, byte[] value) {
        if (device != gatt || !ready || !TELEMETRY.equals(uuid)) return;
        byte[] payload = assembler.feed(value, SystemClock.elapsedRealtime());
        if (payload != null) { lastFrame = SystemClock.elapsedRealtime(); listener.onFrame(payload); }
    }
    public void send(String command) {
        if (command == null || !command.matches("CALSTART|CALCANCEL|SAVE|IMUCAL|SAVEDCD|F[0-5]")) return;
        if (!ready || gatt == null || control == null) { listener.onNotice("Connect to the sensor first."); return; }
        if (writes.size() >= 8) { listener.onNotice("Please wait for the previous command."); return; }
        writes.add(command); nextWrite();
    }
    private void nextWrite() {
        if (writing || writes.isEmpty() || !ready || gatt == null) return;
        byte[] data = writes.remove().getBytes(StandardCharsets.US_ASCII);
        try {
            if (Build.VERSION.SDK_INT >= 33) writing = gatt.writeCharacteristic(control, data,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS;
            else { control.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT); control.setValue(data); writing = gatt.writeCharacteristic(control); }
            if (writing) handler.postDelayed(writeTimeout, 5000);
            else { writes.clear(); listener.onNotice("Could not send command. Retry."); }
        } catch (SecurityException e) { fail("Bluetooth permission revoked.", false); }
    }
}
