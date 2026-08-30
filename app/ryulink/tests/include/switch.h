#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;
typedef int Handle;
typedef struct { int open; } Service;
typedef struct { u8 uuid[16]; } Uuid;
typedef struct { u8 addr[4]; } NifmIpV4Address;
typedef struct {
    u8 is_automatic;
    NifmIpV4Address current_addr;
    NifmIpV4Address subnet_mask;
    NifmIpV4Address gateway;
} NifmIpAddressSetting;
typedef struct {
    NifmIpAddressSetting ip_address_setting;
    u16 mtu;
} NifmIpSettingData;
typedef struct {
    Uuid uuid;
    NifmIpSettingData ip_setting_data;
} NifmNetworkProfileData;
typedef enum { NifmServiceType_User, NifmServiceType_Admin } NifmServiceType;

enum { Module_Libnx = 345, LibnxError_BadInput = 11 };

#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
#define MAKERESULT(module, description) ((Result)(((module) << 9) | (description)))

#ifdef __cplusplus
extern "C" {
#endif

Result svcConnectToNamedPort(Handle *handle, const char *name);
Result smGetService(Service *service, const char *name);
void serviceCreate(Service *service, Handle handle);
void serviceClose(Service *service);
Result serviceDispatchInMock(Service *service, u32 command_id, u32 input);
Result serviceDispatchOutMock(void);
Result serviceDispatchInOutMock(void);
Result serviceDispatchMock(Service *service, u32 command_id);
Result socketInitializeDefault(void);
void socketExit(void);
uint64_t armGetSystemTick(void);
uint64_t armTicksToNs(uint64_t ticks);
void svcSleepThread(uint64_t nanoseconds);
void randomGet(void *out, size_t size);
Result nifmInitialize(NifmServiceType service_type);
void nifmExit(void);
Result nifmGetCurrentNetworkProfile(NifmNetworkProfileData *profile);
Result nifmSetNetworkProfile(const NifmNetworkProfileData *profile, Uuid *uuid);
Result nifmSetWirelessCommunicationEnabled(bool enable);

#ifdef __cplusplus
}
#endif

#define serviceDispatchIn(service, command_id, input) \
    serviceDispatchInMock((service), (command_id), (u32)(input))
#define serviceDispatchOut(service, command_id, output) ((output) = 0, serviceDispatchOutMock())
#define serviceDispatchInOut(service, command_id, input, output) \
    (memset(&(output), 0, sizeof(output)), serviceDispatchInOutMock())
#define serviceDispatch(service, command_id, ...) serviceDispatchMock((service), (command_id))
