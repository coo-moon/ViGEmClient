#include <Windows.h>
#include <SetupAPI.h>
#include <hidsdi.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <cstdio>

#define DS5_VID 0x054C
#define DS5_PID 0x0CE6

DEFINE_GUID(GUID_DEVINTERFACE_HID, 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "=== Scanning HID devices for DualSense ===\n");

    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "SetupDiGetClassDevs failed: 0x%08X\n", GetLastError());
        return 1;
    }

    SP_DEVICE_INTERFACE_DATA ifaceData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    DWORD idx = 0;
    int found = 0;

    while (SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_HID, idx++, &ifaceData)) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0) continue;

        auto detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(requiredSize);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, requiredSize, nullptr, nullptr)) {
            free(detail);
            continue;
        }

        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            free(detail);
            continue;
        }

        HIDD_ATTRIBUTES attribs = { sizeof(HIDD_ATTRIBUTES) };
        if (HidD_GetAttributes(h, &attribs)) {
            if (attribs.VendorID == DS5_VID) {
                fprintf(stderr, "[%d] VID=%04X PID=%04X %s\n", idx, attribs.VendorID, attribs.ProductID,
                    attribs.ProductID == DS5_PID ? "<-- DS5!" : "(not DS5 PID)");
                fprintf(stderr, "    Path: %ls\n", detail->DevicePath);
                found++;

                PHIDP_PREPARSED_DATA preparsed;
                if (HidD_GetPreparsedData(h, &preparsed)) {
                    HIDP_CAPS caps;
                    HidP_GetCaps(preparsed, &caps);
                    fprintf(stderr, "    UsagePage: 0x%04X  InputLen: %d  OutputLen: %d\n",
                        caps.UsagePage, caps.InputReportByteLength, caps.OutputReportByteLength);
                    HidD_FreePreparsedData(preparsed);
                }
            }
        }
        CloseHandle(h);
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    fprintf(stderr, "\nFound %d DualSense HID devices\n", found);
    return 0;
}
