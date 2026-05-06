#include <Windows.h>
#include <SetupAPI.h>
#include <hidsdi.h>
#include <initguid.h>
#include <cstdio>

#define DS5_VID 0x054C
#define DS5_PID 0x0CE6
DEFINE_GUID(GUID_DEVINTERFACE_HID, 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifaceData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    DWORD idx = 0;
    while (SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_HID, idx++, &ifaceData)) {
        DWORD req = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &req, nullptr);
        if (!req) continue;
        auto detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(req);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, req, nullptr, nullptr)) { free(detail); continue; }
        HANDLE h0 = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h0 == INVALID_HANDLE_VALUE) { free(detail); continue; }
        HIDD_ATTRIBUTES a = {sizeof(a)};
        if (HidD_GetAttributes(h0, &a) && a.VendorID == DS5_VID && a.ProductID == DS5_PID) {
            fprintf(stderr, "DS5 path: %ls\n", detail->DevicePath);
            CloseHandle(h0);

            fprintf(stderr, "Opening GENERIC_READ...");
            HANDLE hR = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            fprintf(stderr, " %s (0x%08X)\n", hR!=INVALID_HANDLE_VALUE?"OK":"FAIL", GetLastError());

            fprintf(stderr, "Opening GENERIC_WRITE...");
            HANDLE hW = CreateFileW(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            fprintf(stderr, " %s (0x%08X)\n", hW!=INVALID_HANDLE_VALUE?"OK":"FAIL", GetLastError());

            if (hR != INVALID_HANDLE_VALUE) CloseHandle(hR);
            if (hW != INVALID_HANDLE_VALUE) CloseHandle(hW);
            free(detail); break;
        }
        CloseHandle(h0);
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return 0;
}
