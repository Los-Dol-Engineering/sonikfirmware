/*                                                         
  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                 ,--.                  ,--.
  .--.--.                      ,--.'|              ,--/  /|
 /  /    '.                ,--,:  : |   ,--,    ,---,': / '
|  :  /`. /     ,---.   ,`--.'`|  ' : ,--.'|    :   : '/ / 
;  |  |--`     '   ,'\  |   :  :  | | |  |,     |   '   ,  
|  :  ;_      /   /   | :   |   \ | : `--'_     '   |  /   
 \  \    `.  .   ; ,. : |   : '  '; | ,' ,'|    |   ;  ;   
  `----.   \ '   | |: : '   ' ;.    ; '  | |    :   '   \  
  __ \  \  | '   | .; : |   | | \   | |  | :    |   |    ' 
 /  /`--'  / |   :    | '   : |  ; .' '  : |__  '   : |.  \
'--'.     /   \   \  /  |   | '`--'   |  | '.'| |   | '_\.'
  `--'---'     `----'   '   : |       ;  :    ; '   : |    
                        ;   |.'       |  ,   /  ;   |,'    
                        '---'          ---`-'   '---'      
  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  +++++++++++++++++++LOS DOL ENGINEERING+++++++++++++++++++
  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  Smart hidropONIK Firmware (build06082020)                
    > Sebuah inovasi dalam berhidroponik

  Board:  >ESP32 DEV-KITC   (Untested)
          >WEMOS Lolin32 v2 (Tested)

  TO DO:
    []  Wifi provisioning via Bluetooth serial
            Gunakan bluetooth serial untuk mengatur wifi yang ada 
            cukup dimasukan ssid dan password melalui android
            kemudian keluaran dari android berupa json string berisi
            ssid dan password
    []  Cari tahu kedalaman air (Perhitungannya)
            Cari tahu cara ngukur kedalaman air. Data bak diambil
            dari cloud. Kemudian nanti diitung disini. Fungsinya nanti
            di return presentase level air (int)
*/

//Inisialisasi library/file yang penting
#include <Arduino.h>
#include <esp32-hal-ledc.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "esp32-mqtt.h"
// #include "BluetoothSerial.h"
#include <Servo.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define pin_ds_temp 13
#define pin_tds GPIO_NUM_39
#define pin_sensor_pH GPIO_NUM_36
#define sample_count 30
#define VREF 3.3

#define pompa_mix_a 26
#define pompa_mix_b 27
#define pin_sole 22
#define pin_pH_down 33
#define pin_pH_up 25

#define trigLev 12
#define echoLev 14

String data; //Variabel yang menyimpan data yang akan dikirim

int analogBuffer[sample_count];
int analogBufferTemp[sample_count];
int analogBufferIndex = 0, copyIndex = 0;
float averageVoltage = 0, tdsValue = 0, temperature = 25;
long nilai_TDS = 0;
long nilai_pH = 0;
const int servo = 32;
int pos = 0;
int modeServo;

float ph_setpoint_bawah = 5.5;
float ph_setpoint_atas = 6.5;

OneWire oneWire(pin_ds_temp);
DallasTemperature sensors(&oneWire);

boolean statusStabilisasiTDS = true;
boolean statusStabilisasipH = true;

int set_point = 1000; //Wajib diganti 0

Servo servoProbe;

char server_jam[3], server_menit[3], server_detik[3];
char jam[] = "09";
char menit[] = "00";

char molas[] = "21";
char patmo[] = "45";

float levelAirAktual = 0;
float jumlah = 0;

// TODO: Add mqtt callback for parsing command @Hi-Peng
unsigned long lastMillis = 0; //Penganti delay

long kedalmanAir;

boolean modeSet;

int counter = 0;

void messageReceived(String &topic, String &payload)
{
    Serial.println("incoming: " + topic + " - " + payload);
}

int toInt(char c)
{
    return c - '0';
}

float getLevelAir()
{
    float kedalaman;
    long durasiPantul;
    digitalWrite(trigLev, LOW);
    delayMicroseconds(2);
    digitalWrite(trigLev, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigLev, LOW);
    durasiPantul = (pulseIn(echoLev, HIGH));
    kedalaman = durasiPantul * 0.034 / 2;
    return kedalaman;
}

float rataAir()
{
    float tempKedalaman[10];
    for (int i = 0; i < 10; i++)
    {
        tempKedalaman[i] = 0;
    }

    for (int i = 0; i < 10; i++)
    {
        tempKedalaman[i] = getLevelAir();
        jumlah = jumlah + tempKedalaman[i];
        Serial.print("Mengambil nilai jarak dengan nilai: ");
        Serial.println(tempKedalaman[i]);
        delay(10);
    }
    return jumlah / 10;
}

/*
    Fungsi untuk mengeluarkan carian dari 
    @param channel_pompa    pin output yang digunakan untuk mengeluarkan cairan (int)
            mililiter    banyaknya cairan yang dikeluarkan dalam mL (int)
*/
void pompa(int channel_pompa, int mililiter)
{
    Serial.print("Menghidupkan pompa dan mengeluarkan air");
    Serial.println(mililiter);
    ledcWrite(channel_pompa, 50);
    delay((40 * mililiter));
    ledcWrite(channel_pompa, 0);
    Serial.println("Pompa selesai");
}

int getMedianNum(int bArray[], int iFilterLen)
{
    int bTab[iFilterLen];
    for (byte i = 0; i < iFilterLen; i++)
        bTab[i] = bArray[i];
    int i, j, bTemp;
    for (j = 0; j < iFilterLen - 1; j++)
    {
        for (i = 0; i < iFilterLen - j - 1; i++)
        {
            if (bTab[i] > bTab[i + 1])
            {
                bTemp = bTab[i];
                bTab[i] = bTab[i + 1];
                bTab[i + 1] = bTemp;
            }
        }
    }
    if ((iFilterLen & 1) > 0)
        bTemp = bTab[(iFilterLen - 1) / 2];
    else
        bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
    return bTemp;
}

float get_ppm()
{
    static unsigned long analogSampleTimepoint = millis();
    if (millis() - analogSampleTimepoint > 40U) //every 40 milliseconds,read the analog value from the ADC
    {
        analogSampleTimepoint = millis();
        analogBuffer[analogBufferIndex] = analogRead(pin_tds); //read the analog value and store into the buffer
        Serial.println(analogBuffer[analogBufferIndex]);
        analogBufferIndex++;
        if (analogBufferIndex == sample_count)
            analogBufferIndex = 0;
    }
    static unsigned long printTimepoint = millis();
    if (millis() - printTimepoint > 800U)
    {
        printTimepoint = millis();
        for (copyIndex = 0; copyIndex < sample_count; copyIndex++)
            analogBufferTemp[copyIndex] = analogBuffer[copyIndex];
        averageVoltage = getMedianNum(analogBufferTemp, sample_count) * (float)VREF / 4096.0;                                                                                            // read the analog value more stable by the median filtering algorithm, and convert to voltage value
        float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);                                                                                                               //temperature compensation formula: fFinalResult(25^C) = fFinalResult(current)/(1.0+0.02*(fTP-25.0));
        float compensationVolatge = averageVoltage / compensationCoefficient;                                                                                                            //temperature compensation
        tdsValue = (133.42 * compensationVolatge * compensationVolatge * compensationVolatge - 255.86 * compensationVolatge * compensationVolatge + 857.39 * compensationVolatge) * 0.5; //convert voltage value to tds value
        return tdsValue;
    }
}

/*
    @brief menghidupkan solenoid untuk menambah air
    @param pin_ledeng   Pin kontrol solenoid 
    @param delay    Durasi penghidupan solenoid
    @return void
*/
void hidupkanSolenoid(int pin_ledeng, int durasi)
{
    Serial.print("Menghidupkan solenoid selama ");
    Serial.print(durasi);
    Serial.println(" Detik!");

    digitalWrite(pin_ledeng, HIGH);
    delay(durasi);
    digitalWrite(pin_ledeng, LOW);
}

/*
    Fungsi ini digunakan untuk mengatur naik turunnya servo 
    @param modeServo    Mode "1" untuk turun dan mode "0" untuk naik (int)
*/
void kontrol_servo(int modeServo)
{
    if (modeServo == 1)
    {
        Serial.println("Servo turun start");
        for (pos = 75; pos <= 180; pos += 1)
        {
            servoProbe.write(pos); // tell servo to go to position in variable 'pos'
            delay(15);             // waits 15ms for the servo to reach the position
        }
    }
    else if (modeServo == 0)
    {
        Serial.println("Servo naik start");
        for (pos = 180; pos >= 60; pos -= 1)
        {                          // goes from 180 degrees to 0 degrees
            servoProbe.write(pos); // tell servo to go to position in variable 'pos'
            delay(15);             // waits 15ms for the servo to reach the position
        }
    }
}

float ambil_nilai_pH()
{
    float jumlah = 0;
    float Po[30];
    //Mengosongkan nilai ph
    for (size_t i = 0; i < 30; i++)
    {
        Po[i] = 0;
    }

    //Mengambil data dan menjumlah
    for (int i = 0; i < 30; i++)
    {
        float nilaiPengukuranPh = analogRead(pin_sensor_pH);
        float TeganganPh = 3.3 / 4095.0 * nilaiPengukuranPh;
        Po[i] = 7.00 + ((1.65 - TeganganPh) / 0.17);
        jumlah = jumlah + Po[i];
        Serial.print("NIlai pembacaan raw pH: ");
        Serial.print(Po[i]);
        Serial.print("|| Nilai pembacaan analog: ");
        Serial.print(nilaiPengukuranPh);
        Serial.print("||Pembacaan voltage");
        Serial.println(TeganganPh);
        delay(1000);
    }
    //Hasil rata-rata
    return (float)jumlah / 30;
}

void setup()
{
    Serial.begin(115200);

    ledcSetup(0, 1000, 8);
    ledcSetup(1, 1000, 8);
    ledcSetup(2, 1000, 8);
    ledcSetup(3, 1000, 8);

    ledcAttachPin(pompa_mix_a, 0); // Pompa A
    ledcAttachPin(pompa_mix_b, 1); // Pompa B
    ledcAttachPin(pin_pH_down, 2); // Pompa Down
    ledcAttachPin(pin_pH_up, 3);   // Pompa Up

    pinMode(pompa_mix_a, OUTPUT);
    pinMode(pompa_mix_b, OUTPUT);
    pinMode(pin_pH_up, OUTPUT);
    pinMode(pin_pH_down, OUTPUT);
    pinMode(pin_sole, OUTPUT);
    pinMode(trigLev, OUTPUT);
    pinMode(echoLev, INPUT);
    pinMode(14, INPUT);

    servoProbe.attach(servo, 5);

    Serial.println("Starting device");

    setupCloudIoT();
}

void loop()
{
    DynamicJsonDocument dataJSON(200);

    mqttClient->loop();

    if (!mqttClient->connected())
    {
        connect();
    }

    Serial.println("MAIN LOOP");    
    struct tm timeinfo;
    strftime(server_detik, 3, "%S", &timeinfo);
    strftime(server_menit, 3, "%M", &timeinfo);
    strftime(server_jam, 3, "%H", &timeinfo);

    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Failed to obtain time");
    }

    Serial.print(server_jam);
    Serial.print("  ");
    Serial.print(server_menit);
    Serial.print("  ");
    Serial.println(server_detik);

    //Ngechek data
    if ((strcmp(server_jam, jam) == 0 && strcmp(server_menit, menit) == 0))
    {
        kontrol_servo(1);

        Serial.println("Memulai sekuen pengukuran dan penyesuaian");

        //Mengukur ppm dan pH
        for (int i = 0; i < 30; i++)
        {
            nilai_TDS = get_ppm();
            delay(100);
        }
        Serial.print("Nilai TDS: ");
        Serial.println(nilai_TDS);

        nilai_pH = ambil_nilai_pH();

        kontrol_servo(0);

        Serial.println("Done");

        Serial.print("Nilai TDS: ");
        Serial.print(nilai_TDS);
        Serial.print(" Nilai pH: ");
        Serial.println(nilai_pH);
        // Selesai mengukur ppm dan pH

        //Mengirimkan hasil pengukuran sementara ke internet
        dataJSON["pH"] = nilai_pH;
        dataJSON["levelAir"] = levelAirAktual;
        dataJSON["suhuAir"] = random(0, 100);
        dataJSON["TDS"] = nilai_TDS;
        serializeJson(dataJSON, data);
        Serial.println(data);
        publishTelemetry(data);
        //Selesai mengirim data

        kontrol_servo(1);

        // Memulai penyetaraan permukaan air
        Serial.println("Leveling air");
        modeSet = true;
        while (modeSet == true)
        {
            delay(100);
            levelAirAktual = getLevelAir();
            boolean pumpStatus;

            if (levelAirAktual > 7)
            {
                Serial.print("Menghidupkan solenoid, ketinggian air: ");
                Serial.println(levelAirAktual);
                pumpStatus = true;
            }

            else
            {
                Serial.print("Sudah pas di: ");
                Serial.println(levelAirAktual);
                pumpStatus = false;
            }

            delay(100);

            if (pumpStatus == true)
                digitalWrite(pin_sole, HIGH);

            else
            {
                digitalWrite(pin_sole, LOW);
                modeSet = false;
            }
        }
        //Selesai peratan permukaan air

        kontrol_servo(1);
        Serial.println("Memulai sekuen pengukuran dan penyesuaian");
        for (int i = 0; i < 30; i++)
        {
            nilai_TDS = get_ppm();
            delay(100);
        }
        Serial.print("Nilai TDS: ");
        Serial.println(nilai_TDS);

        nilai_pH = ambil_nilai_pH();
        kontrol_servo(0);
        Serial.println("Done");

        Serial.print("Nilai TDS: ");
        Serial.print(nilai_TDS);
        Serial.print(" Nilai pH: ");
        Serial.println(nilai_pH);

        // TODO: Iki ono masalah ning kene yake @Sopekok, @Hi-Peng #1
        dataJSON["pH"] = nilai_pH;
        dataJSON["levelAir"] = kedalmanAir;
        dataJSON["suhuAir"] = random(0, 100);
        dataJSON["TDS"] = nilai_TDS;
        serializeJson(dataJSON, data);
        Serial.println(data);
        publishTelemetry(data);
        data = ""; //removed this line, might be the culprit

        kontrol_servo(1);
        delay(1000);

        Serial.println("Mempersiapkan stabilisasi TDS");
        statusStabilisasiTDS = true;
        while (statusStabilisasiTDS == true)
        {
            kontrol_servo(0);

            Serial.println("Stablilisasi TDS");
            if (nilai_TDS < (set_point - 50))
            {
                Serial.println("TDS Kurang, akan ditambahkan");

                ledcWrite(0, 100);
                delay((850));   // Sak tutup botol
                ledcWrite(0, 0);

                delay(300000); //Ganti dengan 300000ms (5 menit)

                ledcWrite(1, 100);
                delay((850));
                ledcWrite(1, 0);

                delay(300000); //Ganti dengan 300000ms (5 menit)

                Serial.println("Penambahan Selesai");

                kontrol_servo(1);
                nilai_TDS = get_ppm();
                kontrol_servo(0);

                Serial.print("Nilai TDS: ");
                Serial.println(nilai_TDS);
            }

            else
            {
                Serial.print("TDS Sudah stabil di nilai: ");
                Serial.println(nilai_TDS);
                statusStabilisasiTDS = false;
            }
            
            delay(1000);
            statusStabilisasiTDS = false;
            Serial.println("Selesai TDS");
        }

        // Habis ini stabilisasi pH
        // Serial.println("Memulai stabilisasi pH");
        // while (statusStabilisasipH == true)
        // {
        //     Serial.println("Memulai pH");
        //     if (nilai_pH > ph_setpoint_atas)
        //     {
        //         Serial.println("Nilai pH terlalu tinggi, menurunkan");
        //         //pompa(pin_pH_down, 10);
        //         Serial.println("Mengunggu 5 menit");
        //         delay(300000); //Tunggu 5 menit
        //         Serial.println("Penurunan Selesai");
        //         break;
        //     }
        //     else if (nilai_pH < ph_setpoint_bawah)
        //     {
        //         Serial.println("Nilai pH terlalu rendah, menaikan");
        //         //pompa(pin_pH_up, 10);
        //         Serial.println("Mengunggu 5 menit");
        //         delay(300000); //Tunggu 5 menit
        //         Serial.println("Selesai");
        //         break;
        //     }
        //     else
        //     {
        //         Serial.println("Nilai pH sudah stabil, penyesuaian selesai");
        //         statusStabilisasipH = false;
        //     }
        //     delay(1000);
        //     statusStabilisasipH = false;
        // }

        kontrol_servo(0);
        delay(1000);
        Serial.println("Selesai");
    }

    //Update data tiap waktu
    // TODO: tambah sensor lainnya @Sopekok
    if (counter >= 300)
    {
        dataJSON["pH"] = nilai_pH;
        dataJSON["levelAir"] = kedalmanAir;
        dataJSON["suhuAir"] = random(0, 100);
        dataJSON["TDS"] = nilai_TDS;
        serializeJson(dataJSON, data);
        Serial.println(data);
        publishTelemetry(data);

        counter = 0;
        data = "";
    }

    delay(1000);
    counter++;
}