/*
 * DS5 Bridge - Real DualSense to Virtual DualSense Bridge Tool
 *
 * Reads input from a real Sony DualSense (PS5) controller via HID,
 * forwards it to a virtual DualSense created by ViGEmBus, and passes
 * output reports (rumble, adaptive triggers, LEDs) back to the
 * physical controller for full DualSense feature support.
 *
 * Build requirements:
 *   Windows SDK 10.0.26100+, ViGEmClient library, setupapi.lib, hid.lib
 */

#include <Windows.h>
#include <SetupAPI.h>
#include <hidsdi.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>

// BusShared.h must come before Client.h - it defines DS5_OUTPUT_REPORT
// and DS5_REPORT_EX which Client.h's DS5 callback typedefs reference
#include "../include/ViGEm/km/BusShared.h"
#include "../include/ViGEm/Client.h"

// ============================================================
// Real DualSense HID constants
// ============================================================
#define DS5_VID 0x054C
#define DS5_PID 0x0CE6

// HID report IDs
#define DS5_USB_INPUT_REPORT_ID  0x01
#define DS5_BT_INPUT_REPORT_ID   0x31
#define DS5_USB_OUTPUT_REPORT_ID 0x02
#define DS5_BT_OUTPUT_REPORT_ID  0x31

// Input report sizes
#define DS5_USB_INPUT_SIZE  64
#define DS5_BT_INPUT_SIZE   78

// Output report size (same for USB and BT)
#define DS5_OUTPUT_REPORT_SIZE 48

// HID output report types
#define HID_REPORT_TYPE_OUTPUT 0x02

// GUID for HID device interface
// {4D1E55B2-F16F-11CF-88CB-001111000030}
DEFINE_GUID(GUID_DEVINTERFACE_HID,
    0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB,
    0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

// ============================================================
// Global state for clean shutdown
// ============================================================
static std::atomic<bool> g_running(true);

BOOL WINAPI ConsoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT)
    {
        g_running = false;
        printf("\nShutting down...\n");
        return TRUE;
    }
    return FALSE;
}

// ============================================================
// HID device path discovery for DualSense
// ============================================================
bool FindDualSenseHidDevice(wchar_t* outPath, DWORD pathBufSize,
                            bool* outIsBluetooth,
                            USHORT* outOutputReportLen = nullptr)
{
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_HID, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        printf("[ERROR] SetupDiGetClassDevs failed: 0x%08X\n", GetLastError());
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifaceData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    DWORD idx = 0;
    bool found = false;

    while (SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr,
                                       &GUID_DEVINTERFACE_HID, idx++, &ifaceData))
    {
        // Get required buffer size
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &ifaceData,
                                         nullptr, 0, &requiredSize, nullptr);

        if (requiredSize == 0) continue;

        auto detailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
            malloc(requiredSize));
        if (!detailData) continue;

        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &ifaceData,
                                              detailData, requiredSize,
                                              nullptr, nullptr))
        {
            free(detailData);
            continue;
        }

        // Open device to query attributes
        HANDLE h = CreateFileW(
            detailData->DevicePath,
            0,  // no access needed just for attribute query
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        if (h == INVALID_HANDLE_VALUE)
        {
            free(detailData);
            continue;
        }

        HIDD_ATTRIBUTES attribs = { sizeof(HIDD_ATTRIBUTES) };
        if (HidD_GetAttributes(h, &attribs))
        {
            if (attribs.VendorID == DS5_VID && attribs.ProductID == DS5_PID)
            {
                // Found it. Check if it's USB or BT.
                // On BT, the DualSense uses a different usage page
                // We determine by checking if input report uses BT-style report
                PHIDP_PREPARSED_DATA preparsed;
                if (HidD_GetPreparsedData(h, &preparsed))
                {
                    HIDP_CAPS caps;
                    HidP_GetCaps(preparsed, &caps);

                    bool isBt = (caps.UsagePage == 0xFF00 ||
                                 caps.InputReportByteLength == DS5_BT_INPUT_SIZE);

                    wcscpy_s(outPath, pathBufSize, detailData->DevicePath);
                    *outIsBluetooth = isBt;
                    if (outOutputReportLen)
                        *outOutputReportLen = caps.OutputReportByteLength;
                    found = true;

                    printf("  Found DualSense: %s\n",
                           isBt ? "Bluetooth" : "USB");
                    printf("  Input report length: %d\n", caps.InputReportByteLength);
                    printf("  Output report length: %d\n", caps.OutputReportByteLength);

                    HidD_FreePreparsedData(preparsed);
                }

                CloseHandle(h);
                free(detailData);
                break;
            }
        }

        CloseHandle(h);
        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return found;
}

struct BridgeContext
{
    HANDLE hRealDs5Input;   // handle for reading real DS5 input
    HANDLE hRealDs5Output;  // handle for writing output to real DS5
    PVIGEM_CLIENT vigemClient;
    PVIGEM_TARGET vigemTarget;
    bool isBluetooth;
    USHORT outputReportLen; // OutputReportByteLength from HID caps
};

// ============================================================
// CRC32 matching Linux kernel's crc32_le(0, data, len)
// Raw CRC32: init=0, no final inversion
// ============================================================
static UINT32 StandardCRC32(const UCHAR* data, size_t length)
{
    UINT32 crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

// ============================================================
// Convert DS5_OUTPUT_REPORT to BT output report (78 bytes)
// Maps struct fields individually because the USB struct has
// different padding (Reserved3[6], Reserved4[6]) than the
// BT SetStateData format where triggers are contiguous.
// ============================================================
static int BuildBtOutputReport(const DS5_OUTPUT_REPORT& Report, UCHAR* buf, DWORD bufSize)
{
    memset(buf, 0, bufSize);

    // BT header
    buf[0] = 0x31;  // Report ID
    buf[1] = 0x00;  // Sequence
    buf[2] = 0x10;  // Magic

    // Enable flags (same bit layout as real DualSense)
    buf[3] = Report.FeatureMaskLow;
    buf[4] = Report.FeatureMaskHigh;

    // Rumble
    buf[5] = Report.RumbleLow;
    buf[6] = Report.RumbleHigh;

    // Audio/reserved fields
    memcpy(&buf[7], Report.Reserved1, 4);
    buf[11] = Report.MicLed;
    buf[12] = Report.Reserved2;

    // Right trigger FFB [13..23] (11 bytes)
    memcpy(&buf[13], Report.RightTriggerMotor, 11);

    // Left trigger FFB [24..34] (11 bytes, NO gap between triggers in BT)
    memcpy(&buf[24], Report.LeftTriggerMotor, 11);

    // Player LEDs at [46]
    buf[46] = Report.PlayerLeds;

    // Lightbar RGB at [47..49]
    buf[47] = Report.LightbarR;
    buf[48] = Report.LightbarG;
    buf[49] = Report.LightbarB;

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

// ============================================================
// Output notification callback: virtual DS5 → real DS5
// ============================================================
static void ForwardOutputReport(
    PVIGEM_CLIENT /*Client*/,
    PVIGEM_TARGET /*Target*/,
    DS5_OUTPUT_REPORT Report,
    LPVOID UserData)
{
    auto* ctx = static_cast<BridgeContext*>(UserData);
    DWORD written = 0;

    if (!ctx->isBluetooth)
    {
        // USB: send struct directly with correct report ID
        Report.ReportId = DS5_USB_OUTPUT_REPORT_ID;
        WriteFile(ctx->hRealDs5Output, &Report,
                  sizeof(DS5_OUTPUT_REPORT), &written, nullptr);
    }
    else
    {
        // BT: convert to 78-byte BT format, send via WriteFile
        // with OutputReportByteLength buffer
        DWORD btBufSize = ctx->outputReportLen;
        UCHAR* btBuf = new UCHAR[btBufSize];
        memset(btBuf, 0, btBufSize);

        BuildBtOutputReport(Report, btBuf, btBufSize);

        WriteFile(ctx->hRealDs5Output, btBuf, btBufSize, &written, nullptr);
        delete[] btBuf;
    }

#if 0  // Verbose - uncomment for debugging
    printf("  [OUT→REAL] Rumble: L=%3u H=%3u | RGB: %3u %3u %3u | "
           "TrigR[0]=0x%02X TrigL[0]=0x%02X | Flags=0x%02X 0x%02X\n",
           Report.RumbleLow, Report.RumbleHigh,
           Report.LightbarR, Report.LightbarG, Report.LightbarB,
           Report.RightTriggerMotor[0], Report.LeftTriggerMotor[0],
           Report.FeatureMaskLow, Report.FeatureMaskHigh);
#endif
}

// ============================================================
// Main bridge logic
// ============================================================
int main(int argc, char* argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    bool forceBluetooth = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--bt") == 0 || strcmp(argv[i], "--bluetooth") == 0)
            forceBluetooth = true;
    }

    printf("=== DS5 Bridge: Real DualSense <-> Virtual DualSense ===\n\n");

    // ---- Step 1: Find the real DualSense ----
    printf("[1] Searching for real DualSense controller...\n");

    wchar_t ds5Path[512];
    bool isBluetooth;
    USHORT outputReportLen = 0;

    if (!FindDualSenseHidDevice(ds5Path, ARRAYSIZE(ds5Path), &isBluetooth, &outputReportLen))
    {
        printf("[ERROR] No DualSense controller found.\n");
        printf("        Connect a DualSense via USB or Bluetooth and try again.\n");
        return 1;
    }

    if (forceBluetooth) isBluetooth = true;

    printf("[OK] DualSense found at: %ls\n", ds5Path);

    // ---- Step 2: Open real DS5 for reading and writing ----
    printf("\n[2] Opening real DualSense...\n");

    HANDLE hRead = CreateFileW(
        ds5Path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,  // overlapped for async reads
        nullptr);

    if (hRead == INVALID_HANDLE_VALUE)
    {
        printf("[ERROR] Cannot open DualSense for reading: 0x%08X\n", GetLastError());
        return 1;
    }

    HANDLE hWrite = CreateFileW(
        ds5Path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (hWrite == INVALID_HANDLE_VALUE)
    {
        printf("[ERROR] Cannot open DualSense for writing: 0x%08X\n", GetLastError());
        CloseHandle(hRead);
        return 1;
    }

    printf("[OK] DualSense opened for I/O\n");

    // ---- Step 3: Connect to ViGEmBus ----
    printf("\n[3] Connecting to ViGEmBus...\n");

    PVIGEM_CLIENT client = vigem_alloc();
    if (!client)
    {
        printf("[ERROR] Failed to allocate ViGEm client\n");
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 1;
    }

    VIGEM_ERROR err = vigem_connect(client);
    if (!VIGEM_SUCCESS(err))
    {
        printf("[ERROR] Cannot connect to ViGEmBus driver (0x%08X)\n", err);
        printf("        Is the driver installed? Run: sc query ViGEmBus\n");
        vigem_free(client);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 1;
    }
    printf("[OK] Connected to ViGEmBus\n");

    // ---- Step 4: Create virtual DualSense ----
    printf("\n[4] Creating virtual DualSense...\n");

    PVIGEM_TARGET ds5 = vigem_target_ds5_alloc();
    if (!ds5)
    {
        printf("[ERROR] Failed to allocate DS5 target\n");
        vigem_disconnect(client);
        vigem_free(client);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 1;
    }

    err = vigem_target_add(client, ds5);
    if (!VIGEM_SUCCESS(err))
    {
        printf("[ERROR] Cannot plug in virtual DS5 (0x%08X)\n", err);
        vigem_target_free(ds5);
        vigem_disconnect(client);
        vigem_free(client);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 1;
    }

    printf("[OK] Virtual DualSense plugged in (SerialNo=%lu)\n",
           vigem_target_get_index(ds5));
    printf("     Open \"Set up USB game controllers\" to verify.\n");

    // ---- Step 5: Set up bridge context and output forwarding ----
    printf("\n[5] Setting up output report forwarding...\n");

    BridgeContext ctx = {};
    ctx.hRealDs5Input  = hRead;
    ctx.hRealDs5Output = hWrite;
    ctx.vigemClient    = client;
    ctx.vigemTarget    = ds5;
    ctx.isBluetooth    = isBluetooth;
    ctx.outputReportLen = outputReportLen;

    err = vigem_target_ds5_register_notification(client, ds5,
        ForwardOutputReport, &ctx);

    if (VIGEM_SUCCESS(err))
        printf("[OK] Output report forwarding active\n");
    else
        printf("[WARN] Output notification not available (0x%08X)\n", err);

    // ---- Step 6: Main input processing loop ----
    printf("\n[6] Starting input forwarding...\n");
    printf("     Real DualSense input → Virtual DS5 (60 FPS target)\n");
    printf("     Press Ctrl+C to stop.\n\n");

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // Determine input report size
    DWORD inputReportSize = isBluetooth ? DS5_BT_INPUT_SIZE : DS5_USB_INPUT_SIZE;

    // Allocate read buffer and create OVERLAPPED event
    UCHAR* readBuffer = new UCHAR[inputReportSize];
    OVERLAPPED readOverlapped = {};
    readOverlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (!readOverlapped.hEvent)
    {
        printf("[ERROR] Cannot create event for overlapped I/O\n");
        delete[] readBuffer;
        vigem_target_ds5_unregister_notification(ds5);
        vigem_target_remove(client, ds5);
        vigem_target_free(ds5);
        vigem_disconnect(client);
        vigem_free(client);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 1;
    }

    int pollCount = 0;
    int updateCount = 0;

    while (g_running)
    {
        // Issue an overlapped read
        DWORD bytesRead = 0;
        ResetEvent(readOverlapped.hEvent);

        BOOL readOk = ReadFile(hRead, readBuffer, inputReportSize,
                               &bytesRead, &readOverlapped);

        if (!readOk && GetLastError() != ERROR_IO_PENDING)
        {
            DWORD errCode = GetLastError();
            if (errCode == ERROR_DEVICE_NOT_CONNECTED)
            {
                printf("\n[INFO] DualSense disconnected.\n");
                break;
            }
            printf("[WARN] ReadFile failed: 0x%08X\n", errCode);
            Sleep(100);
            continue;
        }

        // Wait for read completion with timeout
        DWORD waitResult = WaitForSingleObject(readOverlapped.hEvent, 1000);

        if (waitResult == WAIT_TIMEOUT)
        {
            // Cancel the pending I/O
            CancelIo(hRead);
            WaitForSingleObject(readOverlapped.hEvent, INFINITE);
            continue;
        }

        if (!GetOverlappedResult(hRead, &readOverlapped, &bytesRead, FALSE))
        {
            DWORD errCode = GetLastError();
            if (errCode == ERROR_DEVICE_NOT_CONNECTED)
            {
                printf("\n[INFO] DualSense disconnected.\n");
                break;
            }
            if (errCode != ERROR_OPERATION_ABORTED)
                printf("[WARN] GetOverlappedResult failed: 0x%08X\n", errCode);
            continue;
        }

        // Ignore very short reads
        if (bytesRead < 11)
            continue;

        // Map real HID input to DS5_REPORT
        // For USB: report[0] = 0x01 (Report ID), data starts at [1]
        // For BT: report[0] = 0x31, data starts at [1] but with different offsets
        //
        // USB mapping (bytes after report ID match DS5_REPORT directly):
        //   Byte 1→bThumbLX, 2→bThumbLY, 3→bThumbRX, 4→bThumbRY
        //   Byte 5→bTriggerL, 6→bTriggerR, 7→bSequence
        //   Byte 8→bButtonsDPad, 9→bButtonsA, 10→bButtonsB
        //
        // BT mapping: similar but with additional header bytes
        // For now we focus on USB; BT input format needs adjustment

        DS5_REPORT report;

        if (!isBluetooth)
        {
            // USB: data starts at offset 1 (after 0x01 report ID)
            report.bThumbLX    = readBuffer[1];
            report.bThumbLY    = readBuffer[2];
            report.bThumbRX    = readBuffer[3];
            report.bThumbRY    = readBuffer[4];
            report.bTriggerL   = readBuffer[5];
            report.bTriggerR   = readBuffer[6];
            report.bSequence   = readBuffer[7];
            report.bButtonsDPad= readBuffer[8];
            report.bButtonsA   = readBuffer[9];
            report.bButtonsB   = readBuffer[10];
        }
        else
        {
            // Bluetooth: data starts at offset 2
            // BT report: [0]=0x31, [1]=sequence/conn info, [2..11]=input data
            report.bThumbLX    = readBuffer[2];
            report.bThumbLY    = readBuffer[3];
            report.bThumbRX    = readBuffer[4];
            report.bThumbRY    = readBuffer[5];
            report.bTriggerL   = readBuffer[6];
            report.bTriggerR   = readBuffer[7];
            report.bSequence   = readBuffer[8];
            report.bButtonsDPad= readBuffer[9];
            report.bButtonsA   = readBuffer[10];
            report.bButtonsB   = readBuffer[11];
        }

        // Send to virtual DS5
        err = vigem_target_ds5_update(client, ds5, report);
        if (VIGEM_SUCCESS(err))
        {
            updateCount++;
        }

        pollCount++;

        // Status update every ~2 seconds
        if (pollCount % 120 == 0)
        {
            printf("  [STATUS] %d updates sent | "
                   "LStick(%3d,%3d) RStick(%3d,%3d) "
                   "L2=%3d R2=%3d | DPad=0x%02X BtnA=0x%02X\r",
                   updateCount,
                   report.bThumbLX, report.bThumbLY,
                   report.bThumbRX, report.bThumbRY,
                   report.bTriggerL, report.bTriggerR,
                   report.bButtonsDPad, report.bButtonsA);
        }
    }

    // ---- Step 7: Cleanup ----
    printf("\n\n[7] Shutting down...\n");

    CancelIo(hRead);
    CloseHandle(readOverlapped.hEvent);

    printf("  Unplugging virtual DS5...\n");
    vigem_target_ds5_unregister_notification(ds5);
    vigem_target_remove(client, ds5);
    vigem_target_free(ds5);

    printf("  Disconnecting from ViGEmBus...\n");
    vigem_disconnect(client);
    vigem_free(client);

    printf("  Closing real DualSense handles...\n");
    CloseHandle(hRead);
    CloseHandle(hWrite);

    delete[] readBuffer;

    printf("[OK] Done. %d reports sent in %d polls.\n", updateCount, pollCount);
    return 0;
}
