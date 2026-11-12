// ============================================================================
// camera_slot_policy.h — Selection of visible, saved cameras
// ============================================================================

#ifndef CAMERA_SLOT_POLICY_H
#define CAMERA_SLOT_POLICY_H

#include <stdint.h>

int8_t cameraNextVisible(uint8_t visibleMask, uint8_t assignedMask,
                         uint8_t savedCount, int8_t preferred);
uint32_t cameraRetryBackoff(uint32_t current, uint32_t maximum);

#endif // CAMERA_SLOT_POLICY_H
