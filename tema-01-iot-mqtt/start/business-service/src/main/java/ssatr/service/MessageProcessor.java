package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        try {
            while (true) {
                // Așteptăm mesaje din coada specifică
                String message = inbox.take();

                // Salvăm direct mesajul nemodificat în CSV
                storage.append(kind, message);

                try {
                    String[] parts = message.split(";");
                    if (parts.length < 3) throw new IllegalArgumentException("Format mesaj invalid");

                    String deviceId = parts[0];
                    long timestamp = Long.parseLong(parts[1]);

                    switch (kind) {
                        case "telemetry":
                            double temp = Double.parseDouble(parts[2]);
                            dashboard.updateTelemetry(deviceId, temp, timestamp);
                            break;

                        case "alarm":
                            dashboard.updateAlarm(deviceId, timestamp);
                            // Alarmele trebuie afișate și direct în consolă
                            System.out.println("\n[ALARM ALERT] Device: " + deviceId + " | Date: " + message);
                            break;

                        case "status":
                            dashboard.updateStatus(deviceId, timestamp);
                            // Statusul se afișează imediat în consolă
                            System.out.println("\n[STATUS] Device: " + deviceId + " | Mesaj: " + parts[2]);
                            break;
                    }
                } catch (Exception e) {
                    System.out.println("Eroare la procesarea mesajului (" + kind + "): " + message);
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            System.out.println("MessageProcessor pentru " + kind + " a fost oprit.");
        }
    }
}
