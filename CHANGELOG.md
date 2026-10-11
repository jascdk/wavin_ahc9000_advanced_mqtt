# Changelog

## [Unreleased]

- Comfort / Eco / Away presets are now implemented purely on the ESP32 as setpoints. Applying a preset only writes the thermostat setpoint (register 0x02/0x00); the controller's own Eco/Comfort mode bits (register 0x07) are intentionally no longer written because the thermostats only support heat/off and the controller reverted them within one scan. The active preset is tracked per room (persisted across reboots), cleared when the setpoint no longer matches, and the previous manual setpoint is restored when leaving a preset. Added a `Master Away Temp` number entity (`set_master_away_temp`; `set_holiday_temp` is kept as an alias and now also sets this ESP32-side value). Fixed the scan log to print schedule bit 14 and removed the false "mode X -> Y" revert logging.
- Added a `firmware_version` field (sourced from `FIRMWARE_VERSION` in `src/AppConfig.h`) to the MQTT `attributes` payloads for both room channels and the master climate entity, so consumers like Home Assistant can read the ESP gateway firmware version.
- Made GitHub-based OTA updates more robust: `handleGitHubUpdate()` now retries failed downloads (up to 3 attempts with backoff), re-arms certificate/timeout handling on the `objects.githubusercontent.com` redirect on each attempt, and reports real success/failure status back to the web UI via a new `/ota_status` endpoint instead of silently leaving the user on "Updating...".

## [2.3.0] - 2026-09-02

- Aligned C3 Rev. 2 wiring and UART diagnostics with the supported hardware.
- Removed the hardcoded Pico OTA IP and `espota` upload configuration.
- Pinned `TelnetStream` to version `1.3.0`.
- Pointed GitHub release checks to `jascdk/wavin_ahc9000_advanced_mqtt`.
- Documented the bundled Wavin controller update files and clarified that they are not ESP32 gateway firmware.
