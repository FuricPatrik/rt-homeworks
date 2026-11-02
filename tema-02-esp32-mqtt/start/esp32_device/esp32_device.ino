// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   task periodic --(coada de alarme)--> task sporadic
//        |                                     |
//        +---- telemetry ---> MQTT <--- alarm -+
//                              |
//                          commands --> command_received() --(coada de comenzi)--> task aperiodic ---> status
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, valorile inițiale; DEVICE_ID
//                    se citește din adresa MAC a plăcii
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Taskurile le scrieți voi: TODO 0 ... TODO 3.
// Exemplele de pornire sunt în laboratorul 0 din rt-labs-esp32c3; vezi
// exemple/README.md din folderul temei.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Variabile volatile care vor fi modificate de Task-ul aperiodic și citite de restul
volatile uint32_t current_period_ms = PERIOD_MS_INITIAL;
volatile float current_threshold = THRESHOLD_INITIAL;

// Handler-e pentru cozi
QueueHandle_t alarmQueue;
QueueHandle_t commandQueue;

// Structură necesară pentru coada de comenzi (ca textul să fie copiat fizic în memorie)
struct CommandMsg {
    char text[COMMAND_MAX_LEN];
};

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: imediat după pornirea Wi-Fi-ului
// poate fi în jur de 50 °C, apoi scade în câteva minute spre 35-40 °C.
// Rezoluția este de aproximativ 1 °C. Ca să provocați alarme, puneți pragul
// cu SET_THRESHOLD cu un grad sub valoarea citită.
float read_temperature(void) {
    return temperatureRead();
}

// TODO 1: TASK PERIODIC, cu perioada T (inițial PERIOD_MS_INITIAL).
void task_periodic_telemetry(void *pvParameters) {
    // Inițializarea timpului absolut (TickType_t) pentru xTaskDelayUntil
    TickType_t xLastWakeTime = xTaskGetTickCount();

    char payload[128];

    while (1) {
        float temp = read_temperature();
        int64_t timestamp = epoch_ms();

        // Citim valoarea curentă a variabilelor volatile (single source of truth)
        uint32_t period = current_period_ms;
        float threshold = current_threshold;

        // Formăm payload-ul pentru telemetrie: deviceId;timestamp;temperatura
        snprintf(payload, sizeof(payload), "%s;%lld;%.2f", DEVICE_ID, (long long)timestamp, temp);

        // Publicăm
        mqtt_publish("telemetry", payload);

        // Dacă e peste prag, semnalizăm alarma
        if (temp > threshold) {
            // Nu ne blocăm (timeout 0 = nu aşteptă dacă coada e plină)
            xQueueSend(alarmQueue, &temp, 0);
        }

        // Așteptare absolută pe baza perioadei curente (în tick-uri)
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(period));
    }
}

// TODO 2: TASK SPORADIC, tratează alarmele de temperatură.
void task_sporadic_alarm(void *pvParameters) {
    float alarm_temp;
    uint32_t last_release_ms = 0;
    char payload[128];

    while (1) {
        // Așteaptă la infinit un eveniment în coadă (portMAX_DELAY)
        if (xQueueReceive(alarmQueue, &alarm_temp, portMAX_DELAY) == pdPASS) {
            uint32_t now = millis(); // Timpul monoton intern pentru măsurarea intervalului

            // Verificăm dacă a trecut timpul minim (limitarea inundării)
            if (now - last_release_ms < MIN_INTERARRIVAL_MS) {
                Serial.println("[alarm] Eveniment de alarma respins (inundare)");
            } else {
                last_release_ms = now;
                int64_t timestamp = epoch_ms();
                float threshold = current_threshold; // Citim pragul actualizat

                // Formăm payload-ul de alarmă: deviceId;timestamp;temperatura;prag
                snprintf(payload, sizeof(payload), "%s;%lld;%.2f;%.2f", DEVICE_ID, (long long)timestamp, alarm_temp, threshold);

                mqtt_publish("alarm", payload);
                Serial.printf("[alarm] Publicat alarma la temperatura: %.2f\n", alarm_temp);
            }
        }
    }
}

// TODO 3: TASK APERIODIC, execută comenzile primite de la business service.
void task_aperiodic_command(void *pvParameters) {
    CommandMsg cmd;
    char payload[128];
    char responseText[80];

    while (1) {
        // Așteptăm mesaje din coadă (blocant)
        if (xQueueReceive(commandQueue, &cmd, portMAX_DELAY) == pdPASS) {
            // Eliminăm eventualul newline/carriage return de la capăt
            char* ptr = cmd.text;
            while(*ptr) {
                if(*ptr == '\n' || *ptr == '\r') *ptr = '\0';
                ptr++;
            }

            char action[32] = {0};
            char param_str[32] = {0};

            // Extragem prima parte (acțiunea) și a doua parte (parametrul)
            int args_count = sscanf(cmd.text, "%31s %31s", action, param_str);

            bool is_error = false;

            // Procesăm logica
            if (strcasecmp(action, "SET_INTERVAL") == 0) {
                if (args_count >= 2) {
                    current_period_ms = (uint32_t)strtoul(param_str, NULL, 10);
                    snprintf(responseText, sizeof(responseText), "OK %s", cmd.text);
                } else {
                    is_error = true;
                    snprintf(responseText, sizeof(responseText), "ERROR Lipseste ms");
                }
            }
            else if (strcasecmp(action, "SET_THRESHOLD") == 0) {
                if (args_count >= 2) {
                    current_threshold = strtof(param_str, NULL);
                    snprintf(responseText, sizeof(responseText), "OK %s", cmd.text);
                } else {
                    is_error = true;
                    snprintf(responseText, sizeof(responseText), "ERROR Lipseste valoarea");
                }
            }
            else if (strcasecmp(action, "RESET") == 0) {
                current_period_ms = PERIOD_MS_INITIAL;
                current_threshold = THRESHOLD_INITIAL;
                snprintf(responseText, sizeof(responseText), "OK %s", cmd.text);
            }
            else if (strcasecmp(action, "STATUS") == 0) {
                snprintf(responseText, sizeof(responseText), "STATUS interval=%u threshold=%.2f", current_period_ms, current_threshold);
            }
            else {
                is_error = true;
                snprintf(responseText, sizeof(responseText), "ERROR Comanda necunoscuta: %s", action);
            }

            int64_t timestamp = epoch_ms();
            // deviceId;timestamp;text
            snprintf(payload, sizeof(payload), "%s;%lld;%s", DEVICE_ID, (long long)timestamp, responseText);

            mqtt_publish("status", payload);
            Serial.printf("[command] S-a raspuns: %s\n", responseText);
        }
    }
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);

        // Creăm structura ca să copiem textul fizic
        CommandMsg cmd;
        strncpy(cmd.text, text, sizeof(cmd.text) - 1);
        cmd.text[sizeof(cmd.text) - 1] = '\0'; // Siguranță

        // Trimitem structura spre coada de comenzi
        // timeout 0 = nu ne blocăm dacă, printr-o minune, coada e plină
        xQueueSend(commandQueue, &cmd, 0);
}

void setup() {
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
    Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);  // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    wifi_connect();
    time_sync();

    // TODO 0 (a): Crearea cozilor
    // Coada de alarme
    alarmQueue = xQueueCreate(10, sizeof(float));

    // Coada de comenzi
    commandQueue = xQueueCreate(10, sizeof(CommandMsg));

        if (alarmQueue == NULL || commandQueue == NULL) {
            Serial.println("Eroare fatala: Nu s-au putut crea cozile FreeRTOS!");
            while(1) delay(1000); // Ne oprim aici dacă eșuează
        }

    mqtt_start();

    // TODO 0 (b): Pornirea celor 3 task-uri
        // Priority: între 2 și 19. Am pus priorități egale (3) sau se pot eșalona logic.
        // Stivă (Stack Depth): minim 4096 octeți (am folosit 4096).

        xTaskCreate(
            task_periodic_telemetry,   // Funcția task-ului
            "Periodic_Task",           // Nume debug
            4096,                      // Stivă (bytes)
            NULL,                      // Parametri
            3,                         // Prioritate
            NULL                       // Task handle (opțional)
        );

        xTaskCreate(
            task_sporadic_alarm,
            "Sporadic_Task",
            4096,
            NULL,
            4,                         // Am pus prioritate un pic mai mare la alarme
            NULL
        );

        xTaskCreate(
            task_aperiodic_command,
            "Command_Task",
            4096,
            NULL,
            3,
            NULL
        );
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
