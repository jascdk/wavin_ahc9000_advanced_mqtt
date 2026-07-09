#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <WebServer.h>
#include <WavinController.h>
#include <PubSubClient.h>
#include "ConfigManager.h"

class WebInterface {
public:
    WebInterface(int port);
    void begin();
    void handleClient();

private:
    WebServer server;
    String restore_json_buffer;

    // Handlers
    void handleRoot();
    void handleReset();
    void handleReboot();
    void handleUpdate();
    void handleUpdateUpload();
    void handleGitHubCheck();
    void handleGitHubUpdate();
    void handleToggleTelnet();
    void handleToggleDarkMode();
    void handleDiscovery();
    void handleBackup();
    void handleRestore();
    void handleRestoreUpload();
};

#endif