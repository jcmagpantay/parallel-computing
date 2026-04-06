#include <stdio.h>
#include <windows.h>

int main() {
    // Pin this process/thread to Core 2 (bitmask: 0b0100 = 4)
    DWORD_PTR affinityMask = (DWORD_PTR)(1ULL << 2);
    HANDLE currentThread = GetCurrentThread();

    if (SetThreadAffinityMask(currentThread, affinityMask) == 0) {
        printf("Failed to set affinity! Error: %lu\n", GetLastError());
        return 1;
    }

    printf("Thread pinned to Core 2. Check Task Manager -> Performance tab.\n");
    printf("You should see only Core 2 (CPU 2) at 100%% usage.\n");
    printf("Press Ctrl+C to stop.\n");

    // Busy loop to max out the pinned core
    volatile int x = 0;
    while (1) {
        x++;
    }

    return 0;
}
