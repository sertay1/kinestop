/* --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return. - The sys-clk authors
 * --------------------------------------------------------------------------
 *
 * Vendored unchanged from ChargeCap (sys-clk / Horizon-OC lineage) for
 * Kinestop; see ipc_server.h.
 */

#include <string.h>
#include "ipc_server.h"

Result ipcServerInit(IpcServer* server, const char* name, u32 max_sessions) {
    if (max_sessions < 1 || max_sessions > (MAX_WAIT_OBJECTS - 1))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    server->srvName = smEncodeName(name);
    server->max     = max_sessions + 1;
    server->count   = 0;

    Result rc = smRegisterService(&server->handles[0], server->srvName, false, max_sessions);
    if (R_SUCCEEDED(rc))
        server->count = 1;

    return rc;
}

Result ipcServerExit(IpcServer* server) {
    for (u32 i = 0; i < server->count; i++)
        svcCloseHandle(server->handles[i]);

    server->count = 0;
    return smUnregisterService(server->srvName);
}

static Result _addSession(IpcServer* server, Handle session) {
    if (server->count >= server->max)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

    server->handles[server->count] = session;
    server->count++;
    return 0;
}

static Result _deleteSession(IpcServer* server, u32 index) {
    if (!index || index >= server->count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    svcCloseHandle(server->handles[index]);

    for (u32 j = index; j < (server->count - 1); j++)
        server->handles[j] = server->handles[j + 1];

    server->count--;
    return 0;
}

static Result _parseRequest(IpcServerRequest* r) {
    u8* base = (u8*)armGetTls();

    r->hipc      = hipcParseRequest(base);
    r->data.cmdId = 0;
    r->data.size  = 0;
    r->data.ptr   = NULL;

    if (r->hipc.meta.type == CmifCommandType_Request) {
        IpcServerRawHeader* header = (IpcServerRawHeader*)cmifGetAlignedDataStart(r->hipc.data.data_words, base);
        size_t dataSize = r->hipc.meta.num_data_words * 4;

        if (!header || dataSize < sizeof(IpcServerRawHeader) || header->magic != CMIF_IN_HEADER_MAGIC)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);

        r->data.cmdId = header->cmdId;
        if (dataSize > sizeof(IpcServerRawHeader)) {
            r->data.size = dataSize - sizeof(IpcServerRawHeader);
            r->data.ptr  = ((u8*)header) + sizeof(IpcServerRawHeader);
        }
    }

    return 0;
}

static void _prepareResponse(Result rc, const void* data, size_t dataSize) {
    u8* base = (u8*)armGetTls();
    HipcRequest hipc = hipcMakeRequestInline(base,
        .type           = CmifCommandType_Request,
        .num_data_words = (u32)((sizeof(IpcServerRawHeader) + dataSize + 0x10) / 4),
    );

    IpcServerRawHeader* rawHeader = (IpcServerRawHeader*)cmifGetAlignedDataStart(hipc.data_words, base);
    rawHeader->magic  = CMIF_OUT_HEADER_MAGIC;
    rawHeader->result = rc;

    if (R_SUCCEEDED(rc) && dataSize)
        memcpy(((u8*)rawHeader) + sizeof(IpcServerRawHeader), data, dataSize);
}

static Result _processNewSession(IpcServer* server) {
    Handle session;
    Result rc = svcAcceptSession(&session, server->handles[0]);
    if (R_SUCCEEDED(rc) && R_FAILED(rc = _addSession(server, session)))
        svcCloseHandle(session);

    return rc;
}

static Result _processSession(IpcServer* server, IpcServerRequestHandler handler, void* userdata, u32 handleIndex) {
    s32              unusedIndex;
    IpcServerRequest r;
    size_t           dataSize = 0;
    u8               data[IPC_SERVER_MAX_DATA_SIZE];
    bool             close = false;

    Result rc = svcReplyAndReceive(&unusedIndex, &server->handles[handleIndex], 1, 0, UINT64_MAX);
    if (R_SUCCEEDED(rc))
        rc = _parseRequest(&r);

    if (R_SUCCEEDED(rc)) {
        switch (r.hipc.meta.type) {
            case CmifCommandType_Request:
                _prepareResponse(handler(userdata, &r, data, &dataSize), data, dataSize);
                break;
            case CmifCommandType_Close:
                _prepareResponse(0, NULL, 0);
                close = true;
                break;
            default:
                _prepareResponse(MAKERESULT(11, 403), NULL, 0);
                break;
        }

        rc = svcReplyAndReceive(&unusedIndex, &server->handles[handleIndex], 0, server->handles[handleIndex], 0);
        if (rc == KERNELRESULT(TimedOut))
            rc = 0;
    }

    if (R_FAILED(rc) || close)
        _deleteSession(server, handleIndex);

    return rc;
}

Result ipcServerProcess(IpcServer* server, IpcServerRequestHandler handler, void* userdata, u64 timeout) {
    s32    handleIndex = -1;
    Result rc = svcWaitSynchronization(&handleIndex, server->handles, (s32)server->count, timeout);

    if (R_FAILED(rc))
        return rc;

    if (handleIndex < 0 || (u32)handleIndex >= server->count)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    if (handleIndex)
        return _processSession(server, handler, userdata, (u32)handleIndex);

    return _processNewSession(server);
}
