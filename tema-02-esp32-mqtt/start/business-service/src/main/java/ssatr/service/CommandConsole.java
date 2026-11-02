package ssatr.service;

import java.util.Scanner;

/**
 * Consola operatorului: citește comenzi de la tastatură.
 *
 *   show                               afișează dashboard-ul
 *   <deviceId> <COMANDA> [parametru]   trimite o comandă unui device, de exemplu
 *                                      device-1 SET_INTERVAL 1000
 *   help                               afișează comenzile
 *   exit                               oprește serviciul
 */
public class CommandConsole implements Runnable {

    private final MqttConnection mqtt;
    private final Dashboard dashboard;

    public CommandConsole(MqttConnection mqtt, Dashboard dashboard) {
        this.mqtt = mqtt;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        Scanner scanner = new Scanner(System.in);
        printHelp();

        while (scanner.hasNextLine()) {
            String line = scanner.nextLine().trim();

            if (line.isEmpty()) {
                continue;
            } else if (line.equals("exit")) {
                break;
            } else if (line.equals("help")) {
                printHelp();
            } else if (line.equals("show")) {
                dashboard.print();
            } else {
                sendCommand(line);
            }
        }
    }

    private void sendCommand(String line) {
        try {
            // Împărțim linia în două: primul cuvânt este ID-ul, restul e comanda
            String[] parts = line.split(" ", 2);

            if (parts.length < 2 || parts[1].trim().isEmpty()) {
                System.out.println("Eroare: Formatul comenzii este invalid. Ai uitat comanda.");
                printHelp();
                return;
            }

            String deviceId = parts[0];
            String command = parts[1].trim();

            // Publicăm comanda generând topicul dinamic
            String topic = Config.commandTopic(deviceId);
            mqtt.publish(topic, command);
            System.out.println(">> Comanda trimisă către " + deviceId + ": " + command);

        } catch (Exception e) {
            System.out.println("Eroare la trimiterea comenzii: " + e.getMessage());
        }
    }

    private void printHelp() {
        System.out.println("Comenzi: show | <deviceId> SET_INTERVAL <ms> | <deviceId> SET_THRESHOLD <valoare>");
        System.out.println("         <deviceId> RESET | <deviceId> STATUS | help | exit");
    }
}
