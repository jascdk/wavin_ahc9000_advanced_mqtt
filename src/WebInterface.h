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

    // OTA status tracking so the web UI can poll for real success/failure
    // feedback instead of just displaying "Updating..." and hoping for a reboot.
    volatile bool otaInProgress = false;
    volatile bool otaFailed = false;
    String otaLastError;

    // Handlers
    void handleRoot();
    void handleReset();
    void handleReboot();
    void handleUpdate();
    void handleUpdateUpload();
    void handleGitHubCheck();
    void handleGitHubUpdate();
    void handleOtaStatus();
    void handleToggleTelnet();
    void handleToggleDarkMode();
    void handleDiscovery();
    void handleBackup();
    void handleRestore();
    void handleRestoreUpload();
};

#endif