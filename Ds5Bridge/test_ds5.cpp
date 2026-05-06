#include <Windows.h>
#include <cstdio>
#include "../include/ViGEm/km/BusShared.h"
#include "../include/ViGEm/Client.h"

int main() {
    printf("=== Simple X360 Test ===\n");

    printf("[1] vigem_alloc...\n");
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) { printf("FAILED\n"); return 1; }
    printf("OK\n");

    printf("[2] vigem_connect...\n");
    VIGEM_ERROR err = vigem_connect(client);
    if (!VIGEM_SUCCESS(err)) { printf("FAILED: 0x%08X\n", err); return 1; }
    printf("OK\n");

    printf("[3] vigem_target_x360_alloc...\n");
    PVIGEM_TARGET x360 = vigem_target_x360_alloc();
    if (!x360) { printf("FAILED\n"); return 1; }
    printf("OK\n");

    printf("[4] vigem_target_add (X360)...\n");
    err = vigem_target_add(client, x360);
    if (!VIGEM_SUCCESS(err)) {
        printf("FAILED: 0x%08X\n", err);
        printf("Check: Does the driver handle IOCTL_VIGEM_PLUGIN_TARGET?\n");
    } else {
        printf("OK! Serial=%lu\n", vigem_target_get_index(x360));
        Sleep(1000);
        vigem_target_remove(client, x360);
        printf("[5] Removed OK\n");
    }

    vigem_target_free(x360);
    vigem_disconnect(client);
    vigem_free(client);
    return 0;
}
