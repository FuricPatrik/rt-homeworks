    // Semafor pentru pietoni, cu buton, pe ESP32-C3 Super Mini.
//
// Placa este controlerul semaforului; lămpile sunt desenate de dashboard, care
// le aprinde după mesajele "lights" publicate de placă. Patru activități:
//
//   ISR buton BOOT ----+
//                      +--(coada butonului)--> task sporadic
//   command_received --+   (BUTTON)                 |
//                                                   v
//                                         [ starea semaforului ] <-- mutex
//                                            ^          |
//                                            |          v
//                         task periodic de control   task periodic de ieșire
//                         (fazele, T = 100 ms)       (lămpile, T = 500 ms) --> lights
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, pinii, perioadele, duratele fazelor
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Restul îl scrieți voi: TODO 0 ... TODO 3.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Fazele semaforului (vezi tabelul și diagrama din README.md). Numele din
// PHASE_NAMES sunt cele din mesajul "lights", în aceeași ordine ca în enum.
enum Phase { CAR_GREEN, CAR_YELLOW, ALL_RED, PED_GREEN };
const char* const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

// Culorile unei lămpi, cu numele din mesajul "lights".
enum Light { RED, YELLOW, GREEN };
const char* const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

// TODO 0 (c): STAREA SEMAFORULUI, partajată de taskuri și protejată de un
struct TrafficLightState {
    Phase current_phase;
    uint32_t phase_start_ms; // Momentul intrării în faza curentă (millis())
    bool pedestrian_request; // Lampa AȘTEPTAȚI (cererea pietonului)
};

static TrafficLightState g_state;
static SemaphoreHandle_t g_mutex = NULL;

// Un element al cozii pentru apăsări (folosit la TODO 3)
struct ButtonPress {
    uint32_t at_ms;   // Momentul sosirii apăsării (în ms de la pornire)
};

static QueueHandle_t g_button_queue = NULL;

// TODO 1: TASK PERIODIC DE CONTROL, cu perioada CONTROL_PERIOD_MS.
void task_periodic_control(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        uint32_t now = millis();
        Phase before = CAR_GREEN;
        Phase next = CAR_GREEN;

        // Luăm mutexul pentru a evalua și modifica starea
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        before = g_state.current_phase;
        next = before;
        uint32_t elapsed = now - g_state.phase_start_ms;

        // Automatul cu stări
        switch (before) {
            case CAR_GREEN:
                // Trecem în galben dacă a trecut verdele minim ȘI avem cerere de la pietoni
                if (elapsed >= CAR_GREEN_MIN_MS && g_state.pedestrian_request) {
                    next = CAR_YELLOW;
                }
                break;
            case CAR_YELLOW:
                if (elapsed >= CAR_YELLOW_MS) {
                    next = ALL_RED;
                }
                break;
            case ALL_RED:
                if (elapsed >= ALL_RED_MS) {
                    next = PED_GREEN;
                    g_state.pedestrian_request = false; // Ștergem cererea pietonului la primirea verdelui
                }
                break;
            case PED_GREEN:
                if (elapsed >= PED_GREEN_MS) {
                    next = CAR_GREEN;
                }
                break;
        }

        // Dacă s-a schimbat faza, actualizăm starea
        if (next != before) {
            g_state.current_phase = next;
            g_state.phase_start_ms = now;
        }
        xSemaphoreGive(g_mutex);

        // Afișăm pe Serial doar dacă faza s-a schimbat, DUPĂ eliberarea mutexului
        if (next != before) {
            Serial.printf("[control] Schimbare de faza: %s -> %s\n", PHASE_NAMES[before], PHASE_NAMES[next]);
        }

        // Așteptare absolută
        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

// TODO 2: TASK PERIODIC DE IEȘIRE, cu perioada OUTPUT_PERIOD_MS.
void task_periodic_output(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char payload[128];

    for (;;) {
        uint32_t now = millis();

        // Luăm starea local, cu mutexul luat cât mai scurt
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        TrafficLightState s = g_state;
        xSemaphoreGive(g_mutex);

        // Determinăm culorile mașinilor și ale pietonilor
        Light car_light = RED;
        Light ped_light = RED;
        uint32_t sec_remaining = 0;

        switch (s.current_phase) {
            case CAR_GREEN:
                car_light = GREEN;
                ped_light = RED;
                break;
            case CAR_YELLOW:
                car_light = YELLOW;
                ped_light = RED;
                break;
            case ALL_RED:
                car_light = RED;
                ped_light = RED;
                break;
            case PED_GREEN:
                car_light = RED;
                ped_light = GREEN;

                // Calculul numărătorii inverse rotunjite în sus
                uint32_t elapsed = now - s.phase_start_ms;
                if (elapsed < PED_GREEN_MS) {
                    sec_remaining = (PED_GREEN_MS - elapsed + 999) / 1000;
                }
                break;
        }

        // Aprindem sau stingem LED-ul de pe placă pe baza cererii AȘTEPTAȚI (aprins pe LOW)
        digitalWrite(LED_PIN, s.pedestrian_request ? LOW : HIGH);

        // Formăm mesajul mqtt: deviceId;timestamp;faza;masini;pietoni;asteptati;secunde
        int64_t timestamp = epoch_ms();
        snprintf(payload, sizeof(payload), "%s;%lld;%s;%s;%s;%d;%lu",
                 DEVICE_ID,
                 (long long)timestamp,
                 PHASE_NAMES[s.current_phase],
                 LIGHT_NAMES[car_light],
                 LIGHT_NAMES[ped_light],
                 s.pedestrian_request ? 1 : 0,
                 (unsigned long)sec_remaining);

        mqtt_publish("lights", payload);

        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(OUTPUT_PERIOD_MS));
    }
}

// TODO 3: TASK SPORADIC, tratează apăsările butonului pentru pietoni.
void task_sporadic_button(void* pvParameters) {
    uint32_t last_accepted_ms = 0;
    bool any_accepted = false;

    for (;;) {
        ButtonPress p;
        // Se blochează până apare o apăsare în coadă
        xQueueReceive(g_button_queue, &p, portMAX_DELAY);

        uint32_t since = p.at_ms - last_accepted_ms;

        // Interval minim între sosiri pentru debounce și protecție anti-inundare
        if (any_accepted && since < MIN_INTERARRIVAL_MS) {
            Serial.printf("[sporadic] Apasare RESPINSĂ (prea curand, interval = %lu ms)\n", (unsigned long)since);
            continue;
        }

        any_accepted = true;
        last_accepted_ms = p.at_ms;

        // Înregistrăm cererea luând mutexul
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_state.pedestrian_request = true;
        xSemaphoreGive(g_mutex);

        Serial.println("[sporadic] Apasare ACCEPTATĂ, cerere pieton inregistrata (Lampa ASTEPTATI aprinsa).");
    }
}

// TODO 3: RUTINA DE ÎNTRERUPERE a butonului BOOT, înregistrată în setup() cu
void IRAM_ATTR button_isr(void) {
    ButtonPress p = {(uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS)};
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(g_button_queue, &p, &woken);
    portYIELD_FROM_ISR(woken);
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);

    // TODO 3: "BUTTON"
    if (strcmp(text, "BUTTON") == 0) {
            ButtonPress p = {millis()};
            xQueueSend(g_button_queue, &p, 0); // Timeout 0, non-blocant
        }
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
    Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED stins
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați mutexul stării și coada
    g_mutex = xSemaphoreCreateMutex();
    g_button_queue = xQueueCreate(10, sizeof(ButtonPress));

    g_state.current_phase = CAR_GREEN;
        g_state.phase_start_ms = millis();
        g_state.pedestrian_request = false;

        if (g_mutex == NULL || g_button_queue == NULL) {
            Serial.println("Eroare fatala la crearea Mutex-ului sau a Cozii!");
            while(1) delay(1000);
        }


    // TODO 0 (b): porniți cele trei taskuri
    xTaskCreate(task_periodic_control, "Control", 4096, NULL, 4, NULL);
        xTaskCreate(task_periodic_output, "Output", 4096, NULL, 3, NULL);
        xTaskCreate(task_sporadic_button, "Button", 4096, NULL, 4, NULL);

        // Atașăm întreruperea pe falling edge pentru apăsarea butonului BOOT
        attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);


    mqtt_start();
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
