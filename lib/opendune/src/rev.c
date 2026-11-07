/** @file rev.c Version info for the OpenDUNE T-Deck port.
 *
 * The original build system generates this file from rev.c.in via
 * findversion.sh (querying git/svn history). Our PlatformIO build doesn't
 * run that step, so this is a static replacement providing the same three
 * symbols rev.h declares.
 */

#include "types.h"
#include "rev.h"

const char g_opendune_revision[] = "tdeck-port";
const char g_opendune_build_date[] = __DATE__ " " __TIME__;

/* 1 = "unknown state (compiling from sources without any version control
 * software)" per the original rev.c.in's own doc comment - accurate here. */
const uint8 g_opendune_revision_modified = 1;
