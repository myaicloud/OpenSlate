// ============================================================================
// gopro_camera.h — Independent GoPro connection slots
// ============================================================================

#ifndef GOPRO_CAMERA_H
#define GOPRO_CAMERA_H

#include "camera_backend.h"

CameraBackend& gpCamera(uint8_t slot);
void gpStartScan();

#endif // GOPRO_CAMERA_H
