#include <Windows.h>
#include <cstdio>
#include "../include/ViGEm/km/BusShared.h"
#include "../include/ViGEm/Client.h"

int main() {
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) { printf("vigem_alloc failed\n"); return 1; }

    VIGEM_ERROR err = vigem_connect(client);
    if (!VIGEM_SUCCESS(err)) { printf("vigem_connect failed: 0x%08X\n", err); return 1; }
    printf("Connected OK\n");

    PVIGEM_TARGET x360 = vigem_target_x360_alloc();
    if (!x360) { printf("x360_alloc failed\n"); return 1; }

    err = vigem_target_add(client, x360);
    if (!VIGEM_SUCCESS(err)) {
        printf("x360 target_add failed: 0x%08X\n", err);
    } else {
        printf("x360 target_add OK! Serial=%lu\n", vigem_target_get_index(x360));
        vigem_target_remove(client, x360);
    }

    PVIGEM_TARGET ds5 = vigem_target_ds5_alloc();
    if (!ds5) { printf("ds5_alloc failed\n"); return 1; }

    err = vigem_target_add(client, ds5);
    if (!VIGEM_SUCCESS(err)) {
        printf("ds5 target_add failed: 0x%08X\n", err);
    } else {
        printf("ds5 target_add OK! Serial=%lu\n", vigem_target_get_index(ds5));
        vigem_target_remove(client, ds5);
    }

    vigem_target_free(x360);
    vigem_target_free(ds5);
    vigem_disconnect(client);
    vigem_free(client);
    return 0;
}
