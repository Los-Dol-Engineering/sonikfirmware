/*
    TENTANG FILE TEST INI:
        File ini merupakan test dari fitur-fitur yang ada
        Yang ada pada ini berisi program untuk mengambil data dari sistem.
        Nilai variabel yang digunakan akan
        
            Pengambilan dilakukan pada pukul 09:00 WIB (jere virna apik ok,
        aku yo terimo manut ro mbak cantik). Untung saja esp32 memiliki fitur
        untuk mengambil waktu melaui ntp.
            Gunakan fitur time.h untuk mengambil data waktu dari internet.
        Pengambilan waktu dilakukan setiap 1 jam. Setiap 15 menit akan dilakukan
        pengambilan data sensor-sensor sing rodo rapenting (cahaya, suhu udara,
        kelembapan dll). 
    +>      Apabila waktu sudah menunjukan 900 WIB, servo akan menggerakan probe
        untuk masuk ke dalam air. 
    +>      Setelah masuk, probe akan menyetabilkan nilai TDS.
    +>      Setelah TDS baru menyetabilkan nilai pH
    +>      Setelah stabil maka akan kembali ke loop utama yaitu nunggu 15 menit.
        Nunggu 15 menitnya pakainya milis aja

            
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
#include "BluetoothSerial.h"
#include <Servo.h>

#define pin_tds 14
#define sample_count 30a\
#define VREF 3.3

#define pompa_mix_a 26
#define pompa_mix_b 27
#define pin_sole 22
#define pin_pH_down 33
#define pin_pH_up 25

String data; //Variabel yang menyimpan data yang akan dikirim

int analogBuffer[sample_count];
int analogBufferTemp[sample_count];
int analogBufferIndex = 0, copyIndex = 0;
float averageVoltage = 0, tdsValue = 0, temperature = 25;
float nilai_TDS = 0;

float nilai_test_pH = 9;
const int servo = 32;
int pos = 0;
int modeServo;

float ph_setpoint_bawah = 5.5;
float ph_setpoint_atas = 6.5;

boolean statusStabilisasiTDS = true;
boolean statusStabilisasipH = true;

int set_point = 1000; //Wajib diganti 0

Servo servoProbe;

char server_jam[3], server_menit[3], server_detik[3];
char jam[] = "15";
char menit[] = "00";

unsigned long lastMillis = 0; //Penganti delay

/*
    Fungsi untuk mengeluarkan carian dari pompa
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
    @param modeServo    Mode "1" untuk naik dan mode "0" untuk turunv (int)
*/
void kontrol_servo(int modeServo)
{

    switch (modeServo)
    {
    case 0:
        Serial.println("Servo turun start");
        for (pos = 75; pos <= 180; pos += 1)
        {
            servoProbe.write(pos); // tell servo to go to position in variable 'pos'
            delay(15);             // waits 15ms for the servo to reach the position
        }
        break;

    case 1:
        Serial.println("Servo naik start");
        for (pos = 180; pos >= 60; pos -= 1)
        {                          // goes from 180 degrees to 0 degrees
            servoProbe.write(pos); // tell servo to go to position in variable 'pos'
            delay(15);             // waits 15ms for the servo to reach the position
        }
        break;
    default:
        Serial.println("TF? Bukan mode itu cuk");
        break;
    }

    Serial.println("Servo done");
}

void setup()
{
    Serial.begin(115200);

    ledcSetup(0, 1000, 8);
    ledcSetup(1, 1000, 8);
    ledcSetup(2, 1000, 8);
    ledcSetup(3, 1000, 8);

    ledcAttachPin(pompa_mix_a, 0); //Pompa B
    ledcAttachPin(pompa_mix_b, 1); //Pompa B
    ledcAttachPin(pin_pH_down, 2); //Pompa B
    ledcAttachPin(pin_pH_up, 3);   //Pompa B

    pinMode(pompa_mix_a, OUTPUT);
    pinMode(pompa_mix_b, OUTPUT);
    pinMode(pin_pH_up, OUTPUT);
    pinMode(pin_pH_down, OUTPUT);
    pinMode(pin_sole, OUTPUT);
    pinMode(14, INPUT);

    servoProbe.attach(servo, 5);

    Serial.println("Starting device");

    setupCloudIoT();
}

void loop()
{
    DynamicJsonBuffer dataJSON<200>;
    mqttClient->loop();

    if (!mqttClient->connected())
    {
        connect();
    }

    if (Serial.available())
    {
        if (Serial.parseInt() == 1)
        {

            kontrol_servo(0);
            Serial.println("RAPID TDS READ");

            for (int i = 0; i < 30; i++)
            {
                nilai_TDS = get_ppm();
                delay(50);
            }

            Serial.println("Done");
            delay(5000);

            Serial.print("Nilai TDS: ");
            Serial.println(nilai_TDS);

            while (statusStabilisasiTDS == true)
            {
                Serial.println("Stablilisasi TDS");
                if (nilai_TDS < (set_point - 50))
                {
                    Serial.println("TDS Kurang, akan ditambahkan");
                    ledcWrite(0, 100);
                    delay((850));
                    ledcWrite(0, 0);
                    delay(300000); //Ganti dengan 300000ms (5 menit)
                    ledcWrite(1, 100);
                    delay((850));
                    ledcWrite(1, 0);
                    delay(300000); //Ganti dengan 300000ms (5 menit)
                    Serial.println("Penambahan Selesai");
                    nilai_TDS = get_ppm();
                    Serial.print("Nilai TDS: ");
                    Serial.println(nilai_TDS);
                }

                else
                {
                    Serial.print("TDS Sudah stabil di nilai: ");
                    Serial.println(nilai_TDS);
                    statusStabilisasiTDS = false;
                }
                Serial.println("Selesai TDS");
            }

            //Habis ini stabilisasi pH
            // while (statusStabilisasipH == true)
            // {
            //     if (nilai_test_pH > ph_setpoint_atas)
            //     {
            //         Serial.println("Nilai pH terlalu tinggi, menurunkan");
            //         pompa(pin_pH_down, 10);
            //         Serial.println("Mengunggu 5 menit");
            //         delay(10); //Tunggu 5 menit
            //         nilai_test_pH -= 0.5;
            //         Serial.println("Penurunan Selesai");
            //     }
            //     else if (nilai_test_pH < ph_setpoint_bawah)
            //     {
            //         Serial.println("Nilai pH terlalu rendah, menaikan");
            //         pompa(pin_pH_up, 10);
            //         Serial.println("Mengunggu 5 menit");
            //         delay(10); //Tunggu 5 menit
            //         nilai_test_pH += 0.5;
            //         Serial.println("Selesai");
            //     }
            //     else
            //     {
            //         Serial.println("Nilai pH sudah stabil, penyesuaian selesai");
            //         statusStabilisasipH = false;
            //     }
            // }
            dataJSON["pH"] = nilai_ph;
            dataJSON["TDS"] = nilai_TDS;
            dataJSON["suhu_Air"] = random(0, 100);
            dataJSON["level_Air"] = getLevelAir();
            serializeJson(dataJSON, data);
            Serial.println(data);
            publishTelemetry(data);
            delay(1000);
            Serial.println("Servo naik start");
            kontrol_servo(1);
            Serial.println("Selesai");
        }

        else
        {
            Serial.println("NOTHING");
            ledcWrite(1, 0);
            ledcWrite(2, 0);
            ledcWrite(3, 0);
            ledcWrite(4, 0);
        }
    }
}
