// ============================================================================
// camera_slot_policy.cpp — Pure connection scheduling helpers
// ============================================================================

#include "camera_slot_policy.h"

int8_t cameraNextVisible(uint8_t visibleMask, uint8_t assignedMask,
                         uint8_t savedCount, int8_t preferred) {
    uint8_t available = visibleMask & ~assignedMask;
    if (preferred >= 0 && preferred < savedCount && preferred < 8 &&
        (available & (1u << preferred))) return preferred;
    for (uint8_t i = 0; i < savedCount && i < 8; i++) {
        if (available & (1u << i)) return (int8_t)i;
    }
    return -1;
}

uint32_t cameraRetryBackoff(uint32_t current, uint32_t maximum) {
    return current > maximum / 2 ? maximum : current * 2;
}
