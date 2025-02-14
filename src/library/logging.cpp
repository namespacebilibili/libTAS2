/*
    Copyright 2015-2020 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "logging.h"
#include <stdlib.h>
#include "checkpoint/ThreadManager.h" // isMainThread()
#include <unistd.h> // For isatty
#include <cstdarg>
#include <cstring>
#include <inttypes.h> // PRI stuff
#include "frame.h" // For framecount
#include <mutex>
#include <list>
#include "../shared/sockethelpers.h"
#include "../shared/messages.h"
#include "global.h" // Global::shared_config
#include "GlobalState.h"

namespace libtas {

void debuglogstdio(LogCategoryFlag lcf, const char* fmt, ...)
{
    if ((Global::shared_config.includeFlags & LCF_MAINTHREAD) &&
        !ThreadManager::isMainThread())
        return;

    if ((!(lcf & Global::shared_config.includeFlags)  ||
          (lcf & Global::shared_config.excludeFlags)) &&
         !(lcf & LCF_ALERT))
        return;

    /* Not printing anything if global state is set to NOLOG */
    if (GlobalState::isNoLog())
        return;

    /* We avoid recursive loops by protecting eventual recursive calls to debuglog
     * in the following code
     */
     GlobalNoLog tnl;

    /* Build main log string */

    /* We avoid any memory allocation here, because some parts of our code
     * are critical about memory allocation, like checkpointing.
     */
    int maxsize = 2048;
    char s[2048] = {'\0'};
    int size = 0;

    /* We only print colors if displayed on a terminal */
    bool isTerm = isatty(/*cerr*/ 2);
    if (isTerm) {
        if (lcf & LCF_ERROR)
            /* Write the header text in red */
            strncat(s, ANSI_COLOR_RED, maxsize-size-1);
        else if (lcf & LCF_WARNING)
            /* Write the header text in light red */
            strncat(s, ANSI_COLOR_LIGHT_RED, maxsize-size-1);
        else
            /* Write the header text in white */
            strncat(s, ANSI_COLOR_LIGHT_GRAY, maxsize-size-1);
    }
    size = strlen(s);

    snprintf(s + size, maxsize-size-1, "[libTAS f:%" PRIu64 "] ", framecount);
    size = strlen(s);

    pid_t tid;
    if (Global::is_fork)
        /* For forked processes, the thread manager have wrong pid values (those of parent process) */
        NATIVECALL(tid = getpid());
    else
        tid = ThreadManager::getThreadTid();

    snprintf(s + size, maxsize-size-1, "Thread %d %s ", tid, Global::is_fork?"(fork)":(ThreadManager::isMainThread()?"(main)":""));

    size = strlen(s);

    if (isTerm) {
        /* Reset color change */
        strncat(s, ANSI_COLOR_RESET, maxsize-size-1);
        size = strlen(s);
    }

    if (lcf & LCF_ERROR) {
        strncat(s, "ERROR: ", maxsize-size-1);
        size = strlen(s);
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(s + size, maxsize-size-1, fmt, args);
    va_end(args);

    /* If we must send the string to the program for displaying to the user,
     * we only send the actual message */
    // if (lcf & LCF_ALERT) {
    //     setAlertMsg(s+size, strlen(s)-size);
    // }

    size = strlen(s);

    strncat(s, "\n", maxsize-size-1);

    /* We need to use a non-locking function here, because of the following
     * situation (encountered in Towerfall):
     * - Thread 1 starts printing a debug message and acquire the lock
     * - Thread 2 sends a signal to thread 1, interrupting the printing
     * - Thread 1 executes a function that is wrapped, and prints a debug message
     * - Thread 1 wants to acquire the lock
     */
#ifdef __linux__
    fputs_unlocked(s, stderr);
#else
    fputs(s, stderr);
#endif
}

void sendAlertMsg(const std::string alert)
{
    lockSocket();
    sendMessage(MSGB_ALERT_MSG);
    sendString(alert);
    unlockSocket();
}

}
