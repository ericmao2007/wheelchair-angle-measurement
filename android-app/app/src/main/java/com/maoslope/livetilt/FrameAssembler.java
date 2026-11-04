package com.maoslope.livetilt;

import java.io.ByteArrayOutputStream;

/** STATIC2 packets: 0xA6, sequence low byte, fragment index, fragment count. */
public final class FrameAssembler {
    private static final int FRAME_BYTES = 136;
    private static final int FRAGMENTS = 9;
    private final ByteArrayOutputStream buffer = new ByteArrayOutputStream(FRAME_BYTES);
    private int sequence = -1;
    private int next = 0;
    private long started = 0;

    public void reset() { buffer.reset(); sequence = -1; next = 0; started = 0; }

    public byte[] feed(byte[] packet, long nowMillis) {
        if (packet == null || packet.length < 5) { reset(); return null; }
        int marker = packet[0] & 255, seq = packet[1] & 255;
        int index = packet[2] & 255, count = packet[3] & 255;
        int expected = index < FRAGMENTS ? Math.min(16, FRAME_BYTES - index * 16) + 4 : -1;
        if (marker != 0xA6 || count != FRAGMENTS || index >= count || packet.length != expected) {
            reset(); return null;
        }
        if (index == 0) { reset(); sequence = seq; started = nowMillis; }
        if (sequence != seq || index != next || nowMillis < started || nowMillis - started > 500) {
            reset(); return null;
        }
        buffer.write(packet, 4, packet.length - 4);
        next++;
        if (next != FRAGMENTS) return null;
        byte[] payload = buffer.toByteArray();
        reset();
        return payload.length == FRAME_BYTES && (payload[0] & 255) == seq ? payload : null;
    }
}
