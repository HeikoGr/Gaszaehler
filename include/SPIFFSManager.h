#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include <SPIFFS.h>
#include <ArduinoJson.h>

class SPIFFSManager {
public:
    SPIFFSManager();
    ~SPIFFSManager();

    bool begin();
    void end();
    bool saveData(uint32_t pulseCount, uint32_t offset, char* mqtt_server, char* mqtt_port, char *mqtt_user, char *mqtt_password);
    bool loadData(uint32_t& pulseCount, uint32_t& offset, char* mqtt_server, char* mqtt_port, char *mqtt_user, char *mqtt_password);
    void listFiles();

private:
    bool mountSPIFFS();
    static const char* DATA_FILE;
};

#endif // SPIFFS_MANAGER_H