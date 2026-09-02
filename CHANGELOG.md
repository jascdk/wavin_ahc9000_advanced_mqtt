# Changelog

## [Unreleased]

- Added a `firmware_version` field (sourced from `FIRMWARE_VERSION` in `src/AppConfig.h`) to the MQTT `attributes` payloads for both room channels and the master climate entity, so consumers like Home Assistant can read the ESP gateway firmware version.
- Made GitHub-based OTA updates more robust: `handleGitHubUpdate()` now retries failed downloads (up to 3 attempts with backoff), re-arms certificate/timeout handling on the `objects.githubusercontent.com` redirect on each attempt, and reports real success/failure status back to the web UI via a new `/ota_status` endpoint instead of silently leaving the user on "Updating...".

## [2.3.0] - 2026-09-02

- Aligned C3 Rev. 2 wiring and UART diagnostics with the supported hardware.
- Removed the hardcoded Pico OTA IP and `espota` upload configuration.
- Pinned `TelnetStream` to version `1.3.0`.
- Pointed GitHub release checks to `jascdk/wavin_ahc9000_advanced_mqtt`.
- Documented the bundled Wavin controller update files and clarified that they are not ESP32 gateway firmware.
