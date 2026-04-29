/*
 * Test: Send adaptive trigger effects to real DS5 via HID (Bluetooth)
 * Uses the correct BT output report format based on reverse-engineered
 * DualSense protocol from SDL2 and Linux kernel hid-playstation.
 *
 * BT Output Report 0x31 format (78 bytes):
 *   [0]     = 0x31 (Report ID)
 *   [1]     = 0x00 (Sequence)
 *   [2]     = 0x10 (Magic)
 *   [3]     = EnableBits1 (bit0=rumble, bit2=right trigger, bit3=left trigger)
 *   [4]     = EnableBits2 (bit2=LED color)
 *   [5]     = RumbleRight (small/high-freq motor)
 *   [6]     = RumbleLeft  (large/low-freq motor)
 *   [7..12] = Audio/volume fields
 *   [13..23]= Right Trigger FFB (11 bytes: mode + 10 params)
 *   [24..34]= Left Trigger FFB  (11 bytes: mode + 10 params)
 *   [35..46]= Timestamp/motor power/LEDs
 *   [47..49]= Lightbar RGB
 *   [50..73]= Padding (zeros)
 *   [74..77]= CRC32 (raw CRC32, init=0, no inversion, over [0..73])
 *
 * Trigger mode values (raw):
 *   0x05 = Off
 *   0x21 = Feedback (continuous resistance)
 *   0x26 = Vibration
 *   0x25 = Weapon
 */
#include <Windows.h>
#include <SetupAPI.h>
#include <hidsdi.h>
#include <initguid.h>
#include <cstdio>
#include <cstring>

DEFINE_GUID(GUID_DEVINTERFACE_HID,
    0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB,
    0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

#define DS5_VID 0x054C
#define DS5_PID 0x0CE6

// Raw trigger mode values
#define TRIGGER_MODE_OFF        0x05
#define TRIGGER_MODE_FEEDBACK   0x21
#define TRIGGER_MODE_WEAPON     0x25
#define TRIGGER_MODE_VIBRATION  0x26

struct Ds5Info {
    wchar_t path[512];
    bool isBt;
    USHORT inputLen;
    USHORT outputLen;
    USHORT featureLen;
};

bool FindDualSense(Ds5Info* info)
{
    HDEVINFO devs = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA iface = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    DWORD idx = 0;
    bool found = false;

    while (SetupDiEnumDeviceInterfaces(devs, nullptr, &GUID_DEVINTERFACE_HID, idx++, &iface))
    {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devs, &iface, nullptr, 0, &needed, nullptr);
        if (!needed) continue;

        auto detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(needed);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(devs, &iface, detail, needed, nullptr, nullptr))
        { free(detail); continue; }

        HANDLE h = CreateFileW(detail->DevicePath, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { free(detail); continue; }

        HIDD_ATTRIBUTES attr = { sizeof(HIDD_ATTRIBUTES) };
        if (HidD_GetAttributes(h, &attr) && attr.VendorID == DS5_VID && attr.ProductID == DS5_PID)
        {
            PHIDP_PREPARSED_DATA prep;
            if (HidD_GetPreparsedData(h, &prep))
            {
                HIDP_CAPS caps;
                HidP_GetCaps(prep, &caps);

                info->isBt = (caps.InputReportByteLength >= 78);
                info->inputLen = caps.InputReportByteLength;
                info->outputLen = caps.OutputReportByteLength;
                info->featureLen = caps.FeatureReportByteLength;
                wcscpy_s(info->path, detail->DevicePath);

                printf("Found DualSense: %s\n", info->isBt ? "Bluetooth" : "USB");
                printf("  InputLen=%d, OutputLen=%d, FeatureLen=%d\n",
                    info->inputLen, info->outputLen, info->featureLen);

                HidD_FreePreparsedData(prep);
                found = true;
            }
            CloseHandle(h);
            free(detail);
            break;
        }
        CloseHandle(h);
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devs);
    return found;
}

// Standard CRC32 (IEEE 802.3): init=0xFFFFFFFF, final XOR=0xFFFFFFFF
UINT32 StandardCRC32(const UCHAR* data, size_t length)
{
    UINT32 crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

// Build BT output report 0x31 with correct format
int BuildBtReport(UCHAR* buf, DWORD bufSize,
    UCHAR triggerMode, UCHAR triggerForce,
    UCHAR rumbleRight, UCHAR rumbleLeft)
{
    memset(buf, 0, bufSize);

    // BT header
    buf[0] = 0x31;  // Report ID
    buf[1] = 0x00;  // Sequence
    buf[2] = 0x10;  // Magic value (required)

    // EnableBits1 at [3]
    UCHAR enableBits1 = 0;
    if (triggerMode != TRIGGER_MODE_OFF)
        enableBits1 |= 0x04 | 0x08;  // AllowRightTriggerFFB | AllowLeftTriggerFFB
    if (rumbleRight || rumbleLeft)
        enableBits1 |= 0x01;          // EnableRumbleEmulation
    buf[3] = enableBits1;

    // EnableBits2 at [4]
    buf[4] = 0x04;  // AllowLedColor (lightbar visual feedback)

    // Rumble at [5..6]
    buf[5] = rumbleRight;
    buf[6] = rumbleLeft;

    // Right trigger FFB at [13..23] (11 bytes)
    buf[13] = triggerMode;
    if (triggerMode != TRIGGER_MODE_OFF)
    {
        for (int i = 1; i < 11; i++)
            buf[13 + i] = triggerForce;
    }

    // Left trigger FFB at [24..34] (11 bytes)
    buf[24] = triggerMode;
    if (triggerMode != TRIGGER_MODE_OFF)
    {
        for (int i = 1; i < 11; i++)
            buf[24 + i] = triggerForce;
    }

    // Lightbar RGB at [47..49]
    buf[47] = 0x00; // R
    buf[48] = 0xFF; // G
    buf[49] = 0x00; // B

    // CRC32 at [74..77] — standard CRC32 over [0xA2 + report[0..73]]
    UCHAR crcBuf[75];
    crcBuf[0] = 0xA2;
    memcpy(&crcBuf[1], buf, 74);
    UINT32 crc = StandardCRC32(crcBuf, 75);
    buf[74] = (crc >> 0)  & 0xFF;
    buf[75] = (crc >> 8)  & 0xFF;
    buf[76] = (crc >> 16) & 0xFF;
    buf[77] = (crc >> 24) & 0xFF;

    return 78;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("=== DS5 Adaptive Trigger Test v3 ===\n\n");

    Ds5Info info = {};
    if (!FindDualSense(&info))
    {
        printf("[ERROR] DualSense not found.\n");
        return 1;
    }

    if (!info.isBt)
    {
        printf("[WARN] Controller is USB, not Bluetooth. This test is for BT format.\n");
        printf("       USB format uses different offsets. Exiting.\n");
        return 1;
    }

    // Open for writing (WriteFile needs OutputReportByteLength buffer)
    HANDLE hWrite = CreateFileW(info.path, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hWrite == INVALID_HANDLE_VALUE)
    {
        printf("[ERROR] Cannot open for writing: 0x%08X\n", GetLastError());
        return 1;
    }
    printf("[OK] Opened for writing\n\n");

    // Open control handle for HidD_SetOutputReport
    HANDLE hCtrl = CreateFileW(info.path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    printf("[OK] Opened control handle\n\n");

    // Allocate buffers
    // HidD_SetOutputReport: use actual report size (78)
    UCHAR reportBuf[78];
    // WriteFile: must use OutputReportByteLength (547 for BT)
    DWORD fullBufSize = info.outputLen;
    UCHAR* fullBuf = new UCHAR[fullBufSize];

    printf("=== Test 1: Feedback trigger (0x21) via HidD_SetOutputReport ===\n");
    BuildBtReport(reportBuf, sizeof(reportBuf), TRIGGER_MODE_FEEDBACK, 0xFF, 0, 0);
    printf("    Report bytes [0..20]:");
    for (int i = 0; i < 21; i++) printf(" %02X", reportBuf[i]);
    printf("\n    CRC: %02X %02X %02X %02X\n",
        reportBuf[74], reportBuf[75], reportBuf[76], reportBuf[77]);

    BOOL ok = HidD_SetOutputReport(hCtrl, reportBuf, 78);
    printf("    HidD_SetOutputReport(78 bytes): %s (err=0x%08X)\n",
        ok ? "OK" : "FAIL", ok ? 0 : GetLastError());
    printf("    Press L2/R2 - continuous resistance?\n");
    Sleep(5000);

    printf("\n=== Test 2: Reset via HidD_SetOutputReport ===\n");
    BuildBtReport(reportBuf, sizeof(reportBuf), TRIGGER_MODE_OFF, 0x00, 0, 0);
    HidD_SetOutputReport(hCtrl, reportBuf, 78);
    printf("    Reset sent. Press triggers - should be normal.\n");
    Sleep(2000);

    printf("\n=== Test 3: Feedback trigger via WriteFile (full %d-byte buffer) ===\n", fullBufSize);
    memset(fullBuf, 0, fullBufSize);
    BuildBtReport(fullBuf, fullBufSize, TRIGGER_MODE_FEEDBACK, 0xFF, 0, 0);
    DWORD written = 0;
    ok = WriteFile(hWrite, fullBuf, fullBufSize, &written, nullptr);
    printf("    WriteFile(%d bytes): %s (written=%d, err=0x%08X)\n",
        fullBufSize, ok ? "OK" : "FAIL", written, ok ? 0 : GetLastError());
    printf("    Press L2/R2 - continuous resistance?\n");
    Sleep(5000);

    // Reset
    memset(fullBuf, 0, fullBufSize);
    BuildBtReport(fullBuf, fullBufSize, TRIGGER_MODE_OFF, 0x00, 0, 0);
    WriteFile(hWrite, fullBuf, fullBufSize, &written, nullptr);
    Sleep(1000);

    printf("\n=== Test 4: Vibration trigger (0x26) via HidD_SetOutputReport ===\n");
    BuildBtReport(reportBuf, sizeof(reportBuf), TRIGGER_MODE_VIBRATION, 0x40, 0, 0);
    HidD_SetOutputReport(hCtrl, reportBuf, 78);
    printf("    Press L2/R2 - vibration in trigger?\n");
    Sleep(4000);

    // Reset
    BuildBtReport(reportBuf, sizeof(reportBuf), TRIGGER_MODE_OFF, 0x00, 0, 0);
    HidD_SetOutputReport(hCtrl, reportBuf, 78);
    Sleep(1000);

    printf("\n=== Test 5: Rumble only via HidD_SetOutputReport ===\n");
    BuildBtReport(reportBuf, sizeof(reportBuf), TRIGGER_MODE_OFF, 0x00, 100, 200);
    HidD_SetOutputReport(hCtrl, reportBuf, 78);
    printf("    Controller should vibrate for 2s\n");
    Sleep(3000);

    // Cleanup
    printf("\n--- Cleanup ---\n");
    BuildBtReport(reportBuf, sizeof(reportBuf), TRIGGER_MODE_OFF, 0x00, 0, 0);
    HidD_SetOutputReport(hCtrl, reportBuf, 78);

    CloseHandle(hWrite);
    CloseHandle(hCtrl);
    delete[] fullBuf;
    printf("Done.\n");
    return 0;
}
