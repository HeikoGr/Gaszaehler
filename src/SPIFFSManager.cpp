#include <SPIFFS.h>
#include <ArduinoJson.h>

class SPIFFSManager
{
private:
    static const char *DATA_FILE;

public:
    SPIFFSManager();
    ~SPIFFSManager();

    bool begin();
    void end();
    bool saveData(uint32_t pulseCount, uint32_t offset, char *mqtt_server, char *mqtt_port);
    bool loadData(uint32_t &pulseCount, uint32_t &offset, char *mqtt_server, char *mqtt_port);
    void listFiles();

private:
    bool mountSPIFFS();
};

const char *SPIFFSManager::DATA_FILE = "/data.json";

SPIFFSManager::SPIFFSManager() {}

SPIFFSManager::~SPIFFSManager()
{
    end();
}

bool SPIFFSManager::begin()
{
    return mountSPIFFS();
}

void SPIFFSManager::end()
{
    SPIFFS.end();
}

bool SPIFFSManager::mountSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("Fehler beim Mounten von SPIFFS");
        return false;
    }
    return true;
}

bool SPIFFSManager::saveData(uint32_t pulseCount, uint32_t offset, char *mqtt_server, char *mqtt_port)
{
    File file = SPIFFS.open(DATA_FILE, FILE_WRITE);
    if (!file)
    {
        Serial.println("Fehler beim Öffnen der Datei zum Schreiben");
        return false;
    }

    JsonDocument doc;
    doc["count"] = pulseCount;
    doc["offset"] = offset;
    doc["mqtt_server"] = mqtt_server;
    doc["mqtt_port"] = mqtt_port;

    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Fehler beim Schreiben der Daten");
        file.close();
        return false;
    }

    Serial.println("Daten erfolgreich geschrieben:");
    Serial.printf(" < Zählerstand: %i\n", pulseCount);
    Serial.printf(" < Offset: %i\n", offset);
    Serial.printf(" < MQTT Server: %s\n", mqtt_server);
    Serial.printf(" < MQTT Port: %s\n", mqtt_port);

    file.close();
    return true;
}

bool SPIFFSManager::loadData(uint32_t &pulseCount, uint32_t &offset, char *mqtt_server, char *mqtt_port)
{
    File file = SPIFFS.open(DATA_FILE, FILE_READ);
    if (!file)
    {
        Serial.println("Fehler beim Öffnen der Datei zum Lesen");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("Fehler beim Deserialisieren der JSON-Daten");
        return false;
    }

    pulseCount = doc["count"].as<uint32_t>();
    offset = doc["offset"].as<uint32_t>();
    strlcpy(mqtt_server, doc["mqtt_server"] | "", 40);
    strlcpy(mqtt_port, doc["mqtt_port"] | "", 6);

    Serial.printf(" < Zählerstand: %i\n", pulseCount);
    Serial.printf(" < Offset: %i\n", offset);
    Serial.printf(" < MQTT Server: %s\n", mqtt_server);
    Serial.printf(" < MQTT Port: %s\n", mqtt_port);

    return true;
}

void SPIFFSManager::listFiles()
{
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        Serial.print("FILE: ");
        Serial.println(file.name());
        file = root.openNextFile();
    }
}