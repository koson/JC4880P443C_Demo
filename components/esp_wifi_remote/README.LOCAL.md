# ESP WiFi Remote - Local Copy

**Version:** 1.5.0 (locked from espressif/esp_wifi_remote)

## Why Local Copy?

This component is copied locally instead of using the ESP Component Registry because:

1. **Kconfig Fix Required:** The original component has a bug where `orsource` doesn't expand 
   `$ESP_IDF_VERSION` variable, preventing SLAVE_IDF_TARGET options from appearing in menuconfig.

2. **Fixed in:** `Kconfig` line 10
   ```diff
   - orsource "./Kconfig.idf_v$ESP_IDF_VERSION.in"
   + rsource "./Kconfig.idf_v5.5.in"
   ```

3. **Version Lock:** Ensures consistent behavior across builds - no surprise updates.

## Configuration

This component provides WiFi functionality for ESP32-P4 via ESP32-C6 coprocessor using:
- **Transport:** SDIO (optimized for JC4880P443C board)
- **Slave Target:** ESP32-C6
- **Library:** ESP-Hosted

## Updating

If you need to update this component:

1. Download new version to `managed_components/`
2. Apply the Kconfig fix (change `orsource` to `rsource`)
3. Copy to `components/esp_wifi_remote/`
4. Test thoroughly before committing

## Original Source

https://components.espressif.com/components/espressif/esp_wifi_remote
