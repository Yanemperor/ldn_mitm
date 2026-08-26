#pragma once

/* The build supplies this once so NACP metadata and every HTTP request use
 * the identical semantic version. */
#ifndef RYULINK_APP_VERSION
#error "RYULINK_APP_VERSION must be defined by the RyuLink Makefile"
#endif

#define RYULINK_APP_USER_AGENT "RyuLink/" RYULINK_APP_VERSION
