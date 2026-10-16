/*
 * Kinestop overlay - client side of the overlay <-> sysmodule IPC, plus the
 * sysmodule lifecycle (launch / graceful exit / terminate).
 *
 * This is a modification of the original Kinestop by SertAy
 * (https://github.com/sertay1/Kinestop): the dots are now drawn by a
 * sysmodule and this overlay only configures it. Modeled on the ChargeCap
 * overlay's ipc_client and on ovl-sysmodules' launch/shutdown logic.
 */
#pragma once

#include <switch.h>

#include "kinestop_ipc.h"

namespace kinestop::ipc {

    /* Client-side Results, so the GUI can tell them apart from Results the
     * sysmodule or HOS returned. */
    constexpr Result ResultNotInstalled = MAKERESULT(Module_Libnx, LibnxError_NotFound);
    constexpr Result ResultNotRunning   = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    constexpr Result ResultTimeout      = MAKERESULT(Module_Libnx, LibnxError_Timeout);
    constexpr Result ResultApiMismatch  = MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
    constexpr Result ResultExitedEarly  = MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen);

    /* Takes pm:shell and a pm:dmnt reference (libnx ref-counts the latter;
     * the libtesla runtime holds its own). Call from initServices(). */
    Result initialize();

    /* Drops the IPC session and the references initialize() took. Call from
     * exitServices(). */
    void exit();

    /* exefs.nsp (or an exefs/ folder) is present under atmosphere/contents. */
    bool isInstalled();

    /* Fail-fast liveness probe via pm:dmnt: never touches sm. */
    bool isRunning();

    /* Connects to the "kinestop" service. Never blocks: requires the process
     * to be running AND the service to be registered (Atmosphere's
     * AtmosphereHasService), because sm defers GetServiceHandle for names
     * that are not registered yet. Also fetches the API version once. */
    bool connect();
    void disconnect();
    bool connected();

    /* API version reported by the connected sysmodule (0 if not connected). */
    u32 remoteApiVersion();
    bool apiCompatible();

    /* Each call connects first if needed. On any failure the session is
     * dropped so the next call reconnects. */
    Result getApiVersion(u32 *out);
    Result getStatus(KinestopStatus *out);
    Result setConfig(const KinestopWireConfig &cfg);
    Result calibrate(KinestopCalibration *out);
    Result requestExit();

    /* true: the sysmodule takes its VI layer down (keeps sampling) so this
     * panel's Tesla layer is the only extra layer on screen; false: brings
     * it back. Fail-fast when the sysmodule is not running. */
    Result setSuspended(bool suspended);

    /* Launches the sysmodule (if not already running) and waits up to ~1.5s
     * for its service to register, then verifies the API version.
     * Blocking: run it off the render thread. */
    Result start();

    /* Asks the sysmodule to exit over IPC, waits up to ~500ms for the process
     * to disappear and falls back to pmshellTerminateProgram.
     * Blocking: run it off the render thread. */
    Result stop();

}
