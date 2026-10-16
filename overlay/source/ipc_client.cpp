/*
 * Kinestop overlay - IPC client and sysmodule lifecycle.
 *
 * This is a modification of the original Kinestop by SertAy
 * (https://github.com/sertay1/Kinestop). Modeled on the ChargeCap overlay's
 * ipc_client and on ovl-sysmodules' launch/graceful-shutdown logic.
 */

#include "ipc_client.hpp"

#include <sys/stat.h>

namespace kinestop::ipc {

    namespace {
        Service g_srv        = {};
        bool    g_hasSrv     = false;
        u32     g_apiVersion = 0;

        bool    g_pmshellReady = false;
        bool    g_pmdmntReady  = false;

        constexpr const char *ExefsNspPath  = "sdmc:/atmosphere/contents/" KINESTOP_PROGRAM_ID_STR "/exefs.nsp";
        constexpr const char *ExefsNpdmPath = "sdmc:/atmosphere/contents/" KINESTOP_PROGRAM_ID_STR "/exefs/main.npdm";

        constexpr u64 PollIntervalNs  =   50'000'000ULL;  /*  50 ms */
        constexpr u64 StartTimeoutNs  = 1500'000'000ULL;  /* 1.5 s */
        constexpr u64 ExitTimeoutNs   =  500'000'000ULL;  /* 0.5 s */
        constexpr u64 KillTimeoutNs   =  500'000'000ULL;  /* 0.5 s */

        bool fileExists(const char *path) {
            struct stat st;
            return stat(path, &st) == 0;
        }

        /* Atmosphere sm extension (tipc command 65100), same call emuiibo's
         * overlay uses. Unlike GetServiceHandle it never defers, so it is
         * the only safe way to ask "is the name registered yet?" while the
         * sysmodule is still starting up: GetServiceHandle on an unknown
         * name is deferred by sm until someone registers it, which would
         * hang the caller forever if the sysmodule died first. */
        bool serviceRegistered() {
            const SmServiceName name = smEncodeName(KINESTOP_SERVICE_NAME);
            u8 has = 0;
            const Result rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, has);
            return R_SUCCEEDED(rc) && (has & 1);
        }

        /* A failed call usually means the module went away; drop the
         * session so the next connect() can pick it up again. */
        Result check(Result rc) {
            if (R_FAILED(rc))
                disconnect();
            return rc;
        }

        bool waitForExit(u64 timeoutNs) {
            for (u64 waited = 0; ; waited += PollIntervalNs) {
                if (!isRunning())
                    return true;
                if (waited >= timeoutNs)
                    return false;
                svcSleepThread(PollIntervalNs);
            }
        }
    }

    Result initialize() {
        Result rc = pmshellInitialize();
        if (R_SUCCEEDED(rc))
            g_pmshellReady = true;

        /* The libtesla runtime already holds pm:dmnt (__appInit); this just
         * takes our own ref-counted reference so exit() can balance it. */
        const Result rc2 = pmdmntInitialize();
        if (R_SUCCEEDED(rc2))
            g_pmdmntReady = true;

        return R_FAILED(rc) ? rc : rc2;
    }

    void exit() {
        disconnect();
        if (g_pmshellReady) {
            pmshellExit();
            g_pmshellReady = false;
        }
        if (g_pmdmntReady) {
            pmdmntExit();
            g_pmdmntReady = false;
        }
    }

    bool isInstalled() {
        return fileExists(ExefsNspPath) || fileExists(ExefsNpdmPath);
    }

    bool isRunning() {
        u64 pid = 0;
        return R_SUCCEEDED(pmdmntGetProcessId(&pid, KINESTOP_PROGRAM_ID)) && pid != 0;
    }

    bool connect() {
        if (g_hasSrv)
            return true;
        if (!isRunning() || !serviceRegistered())
            return false;

        Handle handle = INVALID_HANDLE;
        if (R_FAILED(smGetServiceOriginal(&handle, smEncodeName(KINESTOP_SERVICE_NAME))))
            return false;

        serviceCreate(&g_srv, handle);
        g_hasSrv = true;

        u32 version = 0;
        if (R_FAILED(serviceDispatchOut(&g_srv, KinestopCmd_GetApiVersion, version))) {
            disconnect();
            return false;
        }
        g_apiVersion = version;
        return true;
    }

    void disconnect() {
        if (!g_hasSrv)
            return;
        serviceClose(&g_srv);
        g_srv        = {};
        g_hasSrv     = false;
        g_apiVersion = 0;
    }

    bool connected() {
        return g_hasSrv;
    }

    u32 remoteApiVersion() {
        return g_apiVersion;
    }

    bool apiCompatible() {
        return g_hasSrv && g_apiVersion == KINESTOP_API_VERSION;
    }

    Result getApiVersion(u32 *out) {
        if (!connect())
            return ResultNotRunning;
        *out = g_apiVersion;
        return 0;
    }

    Result getStatus(KinestopStatus *out) {
        if (!connect())
            return ResultNotRunning;
        if (!apiCompatible())
            return ResultApiMismatch;
        return check(serviceDispatchOut(&g_srv, KinestopCmd_GetStatus, *out));
    }

    Result setConfig(const KinestopWireConfig &cfg) {
        if (!connect())
            return ResultNotRunning;
        if (!apiCompatible())
            return ResultApiMismatch;
        return check(serviceDispatchIn(&g_srv, KinestopCmd_SetConfig, cfg));
    }

    Result calibrate(KinestopCalibration *out) {
        if (!connect())
            return ResultNotRunning;
        if (!apiCompatible())
            return ResultApiMismatch;
        return check(serviceDispatchOut(&g_srv, KinestopCmd_Calibrate, *out));
    }

    /* Exit is void -> void and numbered 3 in every API version (the
     * sys-clk / ovl-sysmodules convention), so it is sent even on an API
     * mismatch. */
    Result requestExit() {
        if (!connect())
            return ResultNotRunning;
        const Result rc = serviceDispatch(&g_srv, KinestopCmd_Exit);
        /* The module is going away either way. */
        disconnect();
        return rc;
    }

    Result start() {
        if (!isRunning()) {
            if (!isInstalled())
                return ResultNotInstalled;

            const NcmProgramLocation location{
                .program_id = KINESTOP_PROGRAM_ID,
                .storageID  = NcmStorageId_None,
            };
            u64 pid = 0;
            const Result rc = pmshellLaunchProgram(0, &location, &pid);
            if (R_FAILED(rc))
                return rc;
        }

        for (u64 waited = 0; ; waited += PollIntervalNs) {
            if (!isRunning())
                return ResultExitedEarly;
            if (connect())
                return apiCompatible() ? 0 : ResultApiMismatch;
            if (waited >= StartTimeoutNs)
                return ResultTimeout;
            svcSleepThread(PollIntervalNs);
        }
    }

    Result stop() {
        if (!isRunning()) {
            disconnect();
            return 0;
        }

        /* Graceful first: lets the sysmodule destroy its VI layer itself. */
        if (R_SUCCEEDED(requestExit()) && waitForExit(ExitTimeoutNs))
            return 0;

        disconnect();
        if (!isRunning())
            return 0;

        const Result rc = pmshellTerminateProgram(KINESTOP_PROGRAM_ID);
        if (R_FAILED(rc))
            return isRunning() ? rc : 0;

        waitForExit(KillTimeoutNs);
        return 0;
    }

}
