#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;
typedef int Handle;
typedef struct { int open; } Service;

#define R_SUCCEEDED(rc) ((rc) >= 0)
#define R_FAILED(rc) ((rc) < 0)

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

#define serviceDispatchIn(service, command_id, input) \
    serviceDispatchInMock((service), (command_id), (u32)(input))
#define serviceDispatchOut(service, command_id, output) ((output) = 0, serviceDispatchOutMock())
#define serviceDispatchInOut(service, command_id, input, output) \
    (memset(&(output), 0, sizeof(output)), serviceDispatchInOutMock())
#define serviceDispatch(service, command_id, ...) serviceDispatchMock((service), (command_id))
