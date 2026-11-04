package com.maoslope.livetilt;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

public class FrameAssemblerTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static byte[] payload(int sequence) {
        byte[] p = new byte[136];
        ByteBuffer b = ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(sequence); b.putInt(123456);
        for (int i=0;i<30;i++) b.putFloat(i+.25f);
        p[128] = -5; p[129] = 61; p[130] = 5; p[131] = 63; p[132] = 5; p[133] = 6;
        return p;
    }
    private static byte[] packet(byte[] frame, int index) {
        int size = Math.min(16, frame.length-index*16);
        byte[] p = new byte[size+4]; p[0] = (byte)0xA6; p[1] = frame[0]; p[2] = (byte)index; p[3] = 9;
        System.arraycopy(frame,index*16,p,4,size); return p;
    }
    private static byte[] complete(FrameAssembler r, byte[] p, long t) {
        byte[] result=null; for(int i=0;i<9;i++) result = r.feed(packet(p,i),t+i); return result;
    }
    public static void main(String[] args) {
        FrameAssembler r = new FrameAssembler(); byte[] p = payload(257);
        check(Arrays.equals(complete(r,p,1000),p),"Exact frame and low-byte wrap");
        r.feed(packet(p,0),2000); r.feed(packet(p,1),2001);
        for(int i=3;i<9;i++) check(r.feed(packet(p,i),2000+i)==null,"Dropped fragment must not decode");
        check(Arrays.equals(complete(r,payload(258),3000),payload(258)),"Recovery after missing packet");
        r.feed(packet(p,0),4000); check(r.feed(packet(p,1),4501)==null,"Fragment timeout");
        r.feed(packet(p,0),5000); check(r.feed(packet(payload(2),1),5001)==null,"Cross-frame sequence");
        r.feed(packet(p,0),6000); check(r.feed(packet(p,2),6002)==null,"Out of order");
        byte[] bad=packet(p,0);bad[0]=(byte)0xA5;check(r.feed(bad,7000)==null,"Legacy protocol rejected");
        bad=packet(p,0);bad[3]=8;check(r.feed(bad,7001)==null,"Wrong count rejected");
        check(r.feed(new byte[0],7002)==null,"Empty packet");
        check(r.feed(Arrays.copyOf(packet(p,8),15),7003)==null,"Malformed last fragment");
        byte[] cross=payload(257);cross[0]=3;
        for(int i=0;i<9;i++) {bad=packet(cross,i);bad[1]=1;check(r.feed(bad,8000+i)==null,"Payload sequence must match");}
        r.feed(packet(p,0),9000);r.reset();check(r.feed(packet(p,1),9001)==null,"Disconnect reset");
        check(Arrays.equals(complete(r,p,10000),p),"Recovery after reset");
        System.out.println("FrameAssembler: 13 reassembly/recovery cases passed");
    }
}
