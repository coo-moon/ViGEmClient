#include <Windows.h>
#include <cstdio>
#include "../include/ViGEm/km/BusShared.h"
#include "../include/ViGEm/Client.h"

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "=== Simple DS5 Test ===\n");

    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) { fprintf(stderr, "[1] vigem_alloc FAILED\n"); return 1; }
    fprintf(stderr, "[1] vigem_alloc OK\n");

    VIGEM_ERROR err = vigem_connect(client);
    if (!VIGEM_SUCCESS(err)) { fprintf(stderr, "[2] vigem_connect FAILED: 0x%08X\n", err); return 1; }
    fprintf(stderr, "[2] vigem_connect OK\n");

    PVIGEM_TARGET ds5 = vigem_target_ds5_alloc();
    if (!ds5) { fprintf(stderr, "[3] ds5_alloc FAILED\n"); return 1; }
    fprintf(stderr, "[3] ds5_alloc OK\n");

    fprintf(stderr, "[4] ds5 target_add...\n");
    err = vigem_target_add(client, ds5);
    if (!VIGEM_SUCCESS(err)) {
        fprintf(stderr, "[4] ds5 target_add FAILED: 0x%08X\n", err);
    } else {
        fprintf(stderr, "[4] ds5 target_add OK! Serial=%lu\n", vigem_target_get_index(ds5));
        Sleep(1000);
        vigem_target_remove(client, ds5);
        fprintf(stderr, "[5] Removed OK\n");
    }

    vigem_target_free(ds5);
    vigem_disconnect(client);
    vigem_free(client);
    return 0;
}
