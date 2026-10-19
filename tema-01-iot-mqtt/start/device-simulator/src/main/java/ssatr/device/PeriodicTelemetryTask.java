package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK PERIODIC, cu perioada T = periodMs: fiecare job citește senzorul și
 * publică valoarea pe topic-ul de telemetrie.
 *
 * Dacă temperatura depășește pragul, taskul NU tratează el alarma, ci doar
 * semnalează un eveniment în coada alarmEvents. Alarma este tratată de
 * taskul sporadic.
 */
public class PeriodicTelemetryTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;

    // volatile: sunt citite aici, dar modificate de taskul aperiodic (alt thread)
    private volatile long periodMs = 2000;
    private volatile double threshold = 28.0;

    public PeriodicTelemetryTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                 BlockingQueue<Double> alarmEvents) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    @Override
    public void run() {
        // Inițializăm timpul monoton absolut
        long nextActivationTime = System.nanoTime();

        try {
            while (true) {
                long currentPeriodMs = this.periodMs;
                // Calculăm timpul următoarei execuții pe grilă (în nanosecunde)
                nextActivationTime += currentPeriodMs * 1_000_000L;

                // Citim temperatura de la senzor
                double temp = sensor.read();
                long timestamp = System.currentTimeMillis();

                // Publicăm cu Locale.US pentru a forța punctul zecimal
                String payload = String.format(Locale.US, "%s;%d;%.2f", deviceId, timestamp, temp);
                mqtt.publish(Config.telemetryTopic(deviceId), payload);

                // Dacă temperatura e peste prag, declanșăm evenimentul în coadă
                if (temp > this.threshold) {
                    alarmEvents.offer(temp);
                }

                // Așteptare absolută: dormim doar cât a mai rămas până la următoarea grilă
                long currentTime = System.nanoTime();
                long waitTimeNs = nextActivationTime - currentTime;

                if (waitTimeNs > 0) {
                    long sleepMs = waitTimeNs / 1_000_000L;
                    int sleepNs = (int) (waitTimeNs % 1_000_000L);
                    Thread.sleep(sleepMs, sleepNs);
                } else {
                    // Dacă s-a depășit deja timpul (delay de procesare imens), realiniem grila
                    nextActivationTime = currentTime;
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            System.out.println("PeriodicTelemetryTask a fost intrerupt.");
        }
    }

    public void setPeriodMs(long periodMs) {
        this.periodMs = periodMs;
    }

    public void setThreshold(double threshold) {
        this.threshold = threshold;
    }

    public long getPeriodMs() {
        return periodMs;
    }

    public double getThreshold() {
        return threshold;
    }
}
