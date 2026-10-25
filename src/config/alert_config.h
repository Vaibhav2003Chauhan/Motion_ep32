#ifndef ALERT_CONFIG_H
#define ALERT_CONFIG_H

#include "../Details/user_details.h"

// -------------------- Mounting Orientation (degrees) --------------------
#define MOUNT_ROLL_DEG             0.0f
#define MOUNT_PITCH_DEG            0.0f
#define MOUNT_YAW_DEG              90.0f

// -------------------- Auto-Calibration --------------------
#define AUTO_CALIB_ENABLE          1       // 1=enabled, 0=disabled
#define AUTO_CALIB_STABLE_SEC      20      // seconds device must stay stable
#define AUTO_CALIB_TOLERANCE_DEG   3.0f    // max angle drift to count as "stable"

// -------------------- BLE Security --------------------
#define BLE_PASSKEY                123456  // 6-digit pairing PIN (phone must enter this)

// -------------------- WiFi --------------------
// No hardcoded SSID/password — user selects via serial at runtime
#define WIFI_SCAN_TIMEOUT_MS       15000
#define WIFI_CONNECT_RETRY_MAX     10      // retries before giving up
#define WIFI_CONNECT_RETRY_DELAY   5000   // ms between retries
#define WIFI_INPUT_TIMEOUT_MS      60000  // 60s to enter password

// -------------------- Tilt / Crash Detection --------------------
#define CRASH_TILT_THRESHOLD_DEG   55.0f
#define CRASH_CONFIRM_COUNT        3
#define CRASH_COOLDOWN_SEC         30

// -------------------- Alert Method Selection --------------------
#define ALERT_ENABLE_EMAIL         1
#define ALERT_ENABLE_SMS           1

// -------------------- Email (SMTP) --------------------
#define ALERT_SMTP_SERVER          "smtp.gmail.com"
#define ALERT_SMTP_PORT            465     // 465=direct SSL (use this), 587=STARTTLS (not supported)
#define ALERT_EMAIL_RECEIVER       "XXXXXXXXXXXXXXX"
#define ALERT_EMAIL_SUBJECT        "CRASH ALERT — LIS3DH"

// -------------------- SMS (Textbelt) --------------------
#define ALERT_SMS_PHONE            "YOUR_NUMBER"
#define ALERT_SMS_API_KEY          "textbelt"

#endif
