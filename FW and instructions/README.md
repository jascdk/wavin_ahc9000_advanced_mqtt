# Wavin AHC 9000 Firmware Files

These files update the Wavin AHC 9000 system, not the ESP32 gateway. Do not upload them through PlatformIO or the gateway web interface.

| File | Target/version mapping |
|---|---|
| `firmware.bin` | Wavin AHC 9000 update image; the version and hardware target are not encoded in the filename. |
| `MC68007.bin` | Wavin AHC 9000 update component identified as `MC68007`; no ESP32 board mapping. |
| `MC61019.bin` | Wavin AHC 9000 update component identified as `MC61019`; no ESP32 board mapping. |

`0680910_AHC_Opdatering_2_Print.pdf` is the supplied Danish update guide. It references a different file set (`MC68005.bin`, `Firmware.bin`, and `MC61017.bin`), so it does not establish a supported version or board mapping for the binaries in this directory. Obtain a matching update package from Wavin before using these files.
