// ============================================================================
// camera_backend.h — One independent camera connection
// ============================================================================

#ifndef CAMERA_BACKEND_H
#define CAMERA_BACKEND_H

#include "camera_common.h"

class CameraBackend {
public:
    virtual ~CameraBackend() {}
    virtual void init() = 0;
    virtual void update(bool allowConnect) = 0;
    virtual void release() = 0;
    virtual void targetMac(const char *mac, uint8_t addressType = 0) = 0;
    virtual bool sendStartRecord() = 0;
    virtual bool sendStopRecord() = 0;
    virtual BleConnectionState getState() const = 0;
    virtual const CameraTelemetry& getTelemetry() const = 0;
    virtual bool isReady() const = 0;
    virtual const char* getLastError() const = 0;
    /// True when the camera refused the last START (e.g. card full, busy).
    /// Cleared by an accepted START, a STOP or a reconnect.
    virtual bool recordRejected() const { return false; }
};

#endif // CAMERA_BACKEND_H
