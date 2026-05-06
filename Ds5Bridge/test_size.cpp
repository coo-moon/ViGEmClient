#include <Windows.h>
#include <cstdio>
#include "../include/ViGEm/km/BusShared.h"

int main() {
    printf("sizeof(VIGEM_PLUGIN_TARGET) = %zu\n", sizeof(VIGEM_PLUGIN_TARGET));
    printf("sizeof(VIGEM_TARGET_TYPE) = %zu\n", sizeof(VIGEM_TARGET_TYPE));
    printf("offset of Size     : %zu\n", offsetof(VIGEM_PLUGIN_TARGET, Size));
    printf("offset of SerialNo : %zu\n", offsetof(VIGEM_PLUGIN_TARGET, SerialNo));
    printf("offset of TargetType: %zu\n", offsetof(VIGEM_PLUGIN_TARGET, TargetType));
    printf("offset of VendorId  : %zu\n", offsetof(VIGEM_PLUGIN_TARGET, VendorId));
    printf("offset of ProductId : %zu\n", offsetof(VIGEM_PLUGIN_TARGET, ProductId));

    VIGEM_PLUGIN_TARGET p;
    VIGEM_PLUGIN_TARGET_INIT(&p, 1, DualSenseWired);
    printf("p.Size = %lu\n", p.Size);
    printf("p.TargetType = %d\n", p.TargetType);
    return 0;
}
