package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 *
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;
    private PeriodicTelemetryTask periodic;

    public SporadicAlarmTask(String deviceId, MqttConnection mqtt, BlockingQueue<Double> alarmEvents, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
        this.periodic=periodic;
    }

    @Override
    public void run() {
        long lastReleaseTime = 0;

        try {
            while (true) {
                // Task-ul adoarme așteptând un eveniment (blocant)
                Double temp = alarmEvents.take();
                long currentTime = System.currentTimeMillis();

                // Verificăm dacă a trecut intervalul minim de protecție împotriva inundării brokerului
                if (currentTime - lastReleaseTime < MIN_INTERARRIVAL_MS) {
                    System.out.println("Alarma respinsa (inundare): " + temp);
                    continue; // respingem și ne întoarcem la așteptat
                }

                lastReleaseTime = currentTime;

                // Preluăm pragul curent din celălalt task (așa cum menționa cerința)
                double currentThreshold = periodic.getThreshold();

                // Publicăm mesajul de alarmă cu pragul curent
                String payload = String.format(Locale.US, "%s;%d;%.2f;%.2f", deviceId, currentTime, temp, currentThreshold);
                mqtt.publish(Config.alarmTopic(deviceId), payload);
                System.out.println("[ALARM START] Trimis: " + payload);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            System.out.println("SporadicAlarmTask a fost intrerupt.");
        }
    }
}
