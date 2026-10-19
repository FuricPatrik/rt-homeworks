package ssatr.service;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 *
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    // Clasă internă pentru a ține starea curentă a unui singur device
    private static class DeviceState {
        String deviceId;
        double lastTemp = 0.0;
        long lastMessageTime = 0;
        int alarmCount = 0;

        DeviceState(String deviceId) {
            this.deviceId = deviceId;
        }
    }

    public void updateTelemetry(String deviceId, double temp, long timestamp) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        synchronized (state) {
            state.lastTemp = temp;
            state.lastMessageTime = timestamp;
        }
    }

    public void updateAlarm(String deviceId, long timestamp) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        synchronized (state) {
            state.alarmCount++;
            state.lastMessageTime = timestamp;
        }
    }

    public void updateStatus(String deviceId, long timestamp) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        synchronized (state) {
            state.lastMessageTime = timestamp;
        }
    }

    private final Map<String, DeviceState> devices = new ConcurrentHashMap<>();

    /** Afișează un tabel cu starea tuturor device-urilor. */
    public void print() {
        System.out.println("==========================================================");
        System.out.println(String.format("%-15s %-10s %-20s %-10s", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME"));
        System.out.println("----------------------------------------------------------");

        long now = System.currentTimeMillis();

        if (devices.isEmpty()) {
            System.out.println("Niciun device conectat încă.");
        } else {
            for (DeviceState state : devices.values()) {
                synchronized (state) {
                    long secondsAgo = (now - state.lastMessageTime) / 1000;
                    String timeStr = "acum " + secondsAgo + " s";

                    System.out.println(String.format("%-15s %-10.2f %-20s %-10d",
                            state.deviceId, state.lastTemp, timeStr, state.alarmCount));
                }
            }
        }
        System.out.println("==========================================================");
    }

}
