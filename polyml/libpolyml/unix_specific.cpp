/*
    Title:      Operating Specific functions: Unix version.

    Copyright (c) 2000 David C. J. Matthews
    Portions of this code are derived from the original stream io
    package copyright CUTS 1983-2000.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/


#ifdef WIN32
#include "winconfig.h"
#else
#include "config.h"
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_SYS_TERMIOS_H
#include <sys/termios.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#include "globals.h"
#include "arb.h"
#include "run_time.h"
#include "io_internal.h"
#include "sys.h"
#include "diagnostics.h"
#include "machine_dep.h"
#include "os_specific.h"
#include "gc.h"
#include "processes.h"
#include "mpoly.h"
#include "sighandler.h"
#include "polystring.h"
#include "save_vec.h"
#include "rts_module.h"

#define STREAMID(x) (DEREFSTREAMHANDLE(x)->streamNo)

#define SAVE(x) gSaveVec->push(x)
#define ALLOC(n) alloc_and_save(n)
#define SIZEOF(x) (sizeof(x)/sizeof(PolyWord))

/* This is used to simulate the alarm clock maintained by "alarm".  Since we
   use SIGALRM for process scheduling we have to handle this internally.  It is
   only approximately accurate and depends on the frequency of the process
   scheduling clock. */
static struct timeval alarmclock = { 0, 0 };

/* Table of constants returned by call 4. */
static int unixConstVec[] =
{
    /* Error codes. */
    E2BIG, /* 0 */
    EACCES,
    EAGAIN,
    EBADF,
#ifdef EBADMSG
/* This is not defined in FreeBSD. */
    EBADMSG,
#else
    0,
#endif
    EBUSY,
#ifdef ECANCELED
/* This is not defined in Linux.  Perhaps somone knows how to spell "cancelled". */
    ECANCELED,
#else
    0, /* Perhaps some other value. */
#endif
    ECHILD,
    EDEADLK,
    EDOM,
    EEXIST,
    EFAULT,
    EFBIG,
    EINPROGRESS,
    EINTR,
    EINVAL,
    EIO,
    EISDIR,
    ELOOP,
    EMFILE,
    EMLINK, /* 20 */
    EMSGSIZE,
    ENAMETOOLONG,
    ENFILE,
    ENODEV,
    ENOENT,
    ENOEXEC,
    ENOLCK,
    ENOMEM,
    ENOSPC,
    ENOSYS,
    ENOTDIR,
    ENOTEMPTY,
#ifdef ENOTSUP
/* Not defined in Linux. */
    ENOTSUP,
#else
    0,
#endif
    ENOTTY,
    ENXIO,
    EPERM,
    EPIPE,
    ERANGE,
    EROFS,
    ESPIPE,
    ESRCH,
    EXDEV, /* 42 */

    /* Signals. */
    SIGABRT, /* 43 */
    SIGALRM,
    SIGBUS,
    SIGFPE,
    SIGHUP,
    SIGILL,
    SIGINT,
    SIGKILL,
    SIGPIPE,
    SIGQUIT,
    SIGSEGV,
    SIGTERM,
    SIGUSR1,
    SIGUSR2,
    SIGCHLD,
    SIGCONT,
    SIGSTOP,
    SIGTSTP,
    SIGTTIN,
    SIGTTOU, /* 62 */

    /* Open flags. */
    O_RDONLY, /* 63 */
    O_WRONLY,
    O_RDWR,
    O_APPEND,
    O_EXCL,
    O_NOCTTY,
    O_NONBLOCK,
#ifdef O_SYNC
    O_SYNC, /* Not defined in FreeBSD. */
#else
    0,
#endif
    O_TRUNC, /* 71 */

    /* TTY: Special characters. */
    VEOF, /* 72 */
    VEOL,
    VERASE,
    VINTR,
    VKILL,
    VMIN,
    VQUIT,
    VSUSP,
    VTIME,
    VSTART,
    VSTOP,
    NCCS, /* 83 */

    /* TTY: Input mode. */
    BRKINT, /* 84 */
    ICRNL,
    IGNBRK,
    IGNCR,
    IGNPAR,
    INLCR,
    INPCK,
    ISTRIP,
    IXOFF,
    IXON,
    PARMRK, /* 94 */

    /* TTY: Output mode. */
    OPOST, /* 95 */

    /* TTY: Control modes. */
    CLOCAL, /* 96 */
    CREAD,
    CS5,
    CS6,
    CS7,
    CS8,
    CSIZE,
    CSTOPB,
    HUPCL,
    PARENB,
    PARODD, /* 106 */

    /* TTY: Local modes. */
    ECHO, /* 107 */
    ECHOE,
    ECHOK,
    ECHONL,
    ICANON,
    IEXTEN,
    ISIG,
    NOFLSH,
    TOSTOP, /* 115 */

    /* TTY: Speeds. */
    B0, /* 116 */
    B50,
    B75,
    B110,
    B134,
    B150,
    B200,
    B300,
    B600,
    B1200,
    B1800,
    B2400,
    B4800,
    B9600,
    B19200,
    B38400, /* 131 */

    /* FD flags. */
    FD_CLOEXEC, /* 132 */

    /* Wait flags. */
    WUNTRACED, /* 133 */
    WNOHANG, /* 134 */

    /* tcsetattr flags. */
    TCSANOW, /* 135 */
    TCSADRAIN,
    TCSAFLUSH,
    /* tcflow flags. */
    TCOOFF, /* 138 */
    TCOON,
    TCIOFF,
    TCION,
    /* tcflush flags. */
    TCIFLUSH, /* 142 */
    TCOFLUSH,
    TCIOFLUSH,
    
    /* File permissions. */
    S_IRUSR, /* 145 */
    S_IWUSR,
    S_IXUSR,
    S_IRGRP,
    S_IWGRP,
    S_IXGRP,
    S_IROTH,
    S_IWOTH,
    S_IXOTH,
    S_ISUID,
    S_ISGID, /* 155 */

    /* Bits for access function. */
    R_OK, /* 156 */
    W_OK,
    X_OK,
    F_OK, /* 159 */
    
    /* Values for lseek. */
    SEEK_SET, /* 160 */
    SEEK_CUR,
    SEEK_END, /* 162 */

    /* Values for lock types. */
    F_RDLCK, /* 163 */
    F_WRLCK,
    F_UNLCK, /* 165 */

    /* Mask for file access. */
    O_ACCMODE, /* 166 */
};

/* Auxiliary functions which implement the more complex cases. */
static Handle waitForProcess(Handle args);
static Handle makePasswordEntry(struct passwd *pw);
static Handle makeGroupEntry(struct group *grp);
static Handle getUname(void);
static Handle getSysConf(Handle args);
static Handle getTTYattrs(Handle args);
static Handle setTTYattrs(Handle args);
static Handle getStatInfo(struct stat *buf);
static Handle lockCommand(int cmd, Handle args);
static int findPathVar(PolyWord ps);

Handle OS_spec_dispatch_c(Handle args, Handle code)
{
    int c = get_C_long(DEREFWORDHANDLE(code));
    switch (c)
    {
    case 0: /* Return our OS type.  Not in any structure. */
        return Make_arbitrary_precision(0); /* 0 for Unix. */

    case 4: /* Return a constant. */
        {
            unsigned i = get_C_long(DEREFWORDHANDLE(args));
            if (i < 0 || i >= sizeof(unixConstVec)/sizeof(unixConstVec[0]))
                raise_syscall("Invalid index", 0);
            return Make_unsigned(unixConstVec[i]);
        }

    case 5: /* fork. */
        {
            pid_t pid = fork();
            if (pid < 0) raise_syscall("fork failed", errno);
            return Make_unsigned(pid);
        }

    case 6: /* kill */
        {
            int pid = get_C_long(DEREFHANDLE(args)->Get(0));
            int sig = get_C_long(DEREFHANDLE(args)->Get(1));
            if (kill(pid, sig) < 0) raise_syscall("kill failed", errno);
            return Make_unsigned(0);
        }

    case 7: /* get process id */
        {
            pid_t pid = getpid();
            if (pid < 0) raise_syscall("getpid failed", errno);
            return Make_unsigned(pid);
        }

    case 8: /* get process id of parent */
        {
            pid_t pid = getppid();
            if (pid < 0) raise_syscall("getppid failed", errno);
            return Make_unsigned(pid);
        }

    case 9: /* get real user id */
        {
            uid_t uid = getuid();
            if (uid < 0) raise_syscall("getuid failed", errno);
            return Make_unsigned(uid);
        }

    case 10: /* get effective user id */
        {
            uid_t uid = geteuid();
            if (uid < 0) raise_syscall("geteuid failed", errno);
            return Make_unsigned(uid);
        }

    case 11: /* get real group id */
        {
            gid_t gid = getgid();
            if (gid < 0) raise_syscall("getgid failed", errno);
            return Make_unsigned(gid);
        }

    case 12: /* get effective group id */
        {
            gid_t gid = getegid();
            if (gid < 0) raise_syscall("getegid failed", errno);
            return Make_unsigned(gid);
        }

    case 13: /* Return process group */
        {
            pid_t pid = getpgrp();
            if (pid < 0) raise_syscall("getpgrp failed", errno);
            return Make_unsigned(pid);
        }

    case 14: /* Wait for child process to terminate. */
        return waitForProcess(args);

    case 15: /* Unpack a process result. */
        {
            int resType, resVal;
            Handle result,  typeHandle, resHandle;
            int status = get_C_long(DEREFWORDHANDLE(args));
            if (WIFEXITED(status))
            {
                resType = 1;
                resVal = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                resType = 2;
                resVal = WTERMSIG(status);
            }
            else if (WIFSTOPPED(status))
            {
                resType = 3;
                resVal = WSTOPSIG(status);
            }
            else { /* ?? */
                resType = 0;
                resVal = 0;
            }
            typeHandle = Make_arbitrary_precision(resType);
            resHandle = Make_arbitrary_precision(resVal);

            result = ALLOC(2);
            DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(typeHandle));
            DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(resHandle));
            return result;
        }

    case 16: /* Pack up a process result.  The inverse of the previous call. */
        {
            int resType = get_C_long(DEREFHANDLE(args)->Get(0));
            int resVal = get_C_long(DEREFHANDLE(args)->Get(1));
            int result = 0;
            switch (resType)
            {
            case 1: /* Exited */ result = resVal << 8; break;
            case 2: /* Signalled */ result = resVal; break;
            case 3: /* Stopped */ result = (resVal << 8) | 0177;
            }
            return Make_unsigned(result);
        }

    case 17: /* Run a new executable. */
        {
            char *path = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            char **argl = stringListToVector(SAVE(DEREFHANDLE(args)->Get(1)));
            int err;
            execv(path, argl);
            err = errno;
            /* We only get here if there's been an error. */
            free(path);
            freeStringVector(argl);
            raise_syscall("execv failed", err);
        }

    case 18: /* Run a new executable with given environment. */
        {
            char *path = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            char **argl = stringListToVector(SAVE(DEREFHANDLE(args)->Get(1)));
            char **envl = stringListToVector(SAVE(DEREFHANDLE(args)->Get(2)));
            int err;
            execve(path, argl, envl);
            err = errno;
            /* We only get here if there's been an error. */
            free(path);
            freeStringVector(argl);
            freeStringVector(envl);
            raise_syscall("execve failed", err);
        }

    case 19: /* Run a new executable using PATH environment variable. */
        {
            char *path = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            char **argl = stringListToVector(SAVE(DEREFHANDLE(args)->Get(1)));
            int err;
            execvp(path, argl);
            err = errno;
            /* We only get here if there's been an error. */
            free(path);
            freeStringVector(argl);
            raise_syscall("execvp failed", err);
        }

    case 20: /* Sets an alarm and returns the current alarm time.  The caller (within
            the Posix structure) converts these from/to relative times. */
        {
            /* We have a value in microseconds.  We need to split
               it into seconds and microseconds. */
            Handle hTime = args;
            Handle hMillion = Make_arbitrary_precision(1000000);
            int secs = get_C_long(DEREFWORDHANDLE(div_longc(hMillion, hTime)));
            int usecs = get_C_long(DEREFWORDHANDLE(rem_longc(hMillion, hTime)));
            Handle result = /* Return the previous setting. */
                Make_arb_from_pair_scaled(alarmclock.tv_sec,
                        alarmclock.tv_usec, 1000000);
            /* Store the absolute time. */
            alarmclock.tv_sec = secs;
            alarmclock.tv_usec = usecs;
            return result;
        }

    case 21: /* Pause until signal. */
        {
            /* This never returns.  When a signal is handled it will
               be interrupted. */
            processes->block_and_restart(-1, 1 /* Interruptable. */, POLY_SYS_os_specific);
        }

    case 22: /* Sleep until given time or until a signal.  Note: this is called
            with an absolute time as an argument and returns a relative time as
            result.  This RTS call is tried repeatedly until either the time has
            expired or a signal has occurred. */
        {
            struct timeval tv;
            struct timezone tz;
            /* We have a value in microseconds.  We need to split
               it into seconds and microseconds. */
            Handle hTime = args;
            Handle hMillion = Make_arbitrary_precision(1000000);
            unsigned long secs = get_C_ulong(DEREFWORDHANDLE(div_longc(hMillion, hTime)));
            unsigned long usecs = get_C_ulong(DEREFWORDHANDLE(rem_longc(hMillion, hTime)));
            /* Has the time expired? */
            if (gettimeofday(&tv, &tz) != 0)
                raise_syscall("gettimeofday failed", errno);
            /* If the timeout time is earlier than the current time
               we must return, otherwise we block.  This can be interrupted
               by a signal. */
            if ((unsigned long)tv.tv_sec < secs ||
                ((unsigned long)tv.tv_sec == secs && (unsigned long)tv.tv_usec < usecs))
                processes->block_and_restart(-1, 1 /* Interruptable. */, POLY_SYS_os_specific);
            return Make_arbitrary_precision(0);
        }
    
    case 23: /* Set uid. */
        {
            uid_t uid = get_C_long(DEREFWORDHANDLE(args));
            if (setuid(uid) != 0) raise_syscall("setuid failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 24: /* Set gid. */
        {
            gid_t gid = get_C_long(DEREFWORDHANDLE(args));
            if (setgid(gid) != 0) raise_syscall("setgid failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 25: /* Get group list. */
        {
            gid_t groups[NGROUPS_MAX];
            int ngroups = getgroups(NGROUPS_MAX, groups);
            if (ngroups < 0) raise_syscall("getgroups failed", errno);
            Handle saved = gSaveVec->mark();
            Handle list  = SAVE(ListNull);

            /* It's simplest to process the integers in reverse order */
            while (--ngroups >= 0)
            {
                Handle value = Make_arbitrary_precision(groups[ngroups]);
                Handle next  = ALLOC(SIZEOF(ML_Cons_Cell));
                DEREFLISTHANDLE(next)->h = DEREFWORDHANDLE(value); 
                DEREFLISTHANDLE(next)->t = DEREFLISTHANDLE(list);
                gSaveVec->reset(saved);
                list = SAVE(DEREFHANDLE(next));
            }
            return list;
        }

    case 26: /* Get login name. */
        {
            char *login = getlogin();
            if (login == 0) raise_syscall("getlogin failed", errno);
            return SAVE(C_string_to_Poly(login));
        }
        
    case 27: /* Set sid */
        {
            pid_t pid = setsid();
            if (pid < 0) raise_syscall("setsid failed", errno);
            return Make_arbitrary_precision(pid);
        }

    case 28: /* Set process group. */
        {
            pid_t pid = get_C_long(DEREFHANDLE(args)->Get(0));
            pid_t pgid = get_C_long(DEREFHANDLE(args)->Get(1));
            if (setpgid(pid, pgid) < 0 ) raise_syscall("setpgid failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 29: /* uname */ return getUname();

    case 30: /* Get controlling terminal. */
        {
            char *term = ctermid(0);
            /* Can this generate an error? */
            if (term == 0) raise_syscall("ctermid failed", errno);
            return SAVE(C_string_to_Poly(term));
        }

    case 31: /* Get terminal name for file descriptor. */
        {
            PIOSTRUCT str = get_stream(args->WordP());
            char *term;
            if (str == NULL) raise_syscall("Stream is closed", EBADF);
            term = ttyname(str->device.ioDesc);
            if (term == 0) raise_syscall("ttyname failed", errno);
            return SAVE(C_string_to_Poly(term));
        }

    case 32: /* Test if file descriptor is a terminal.  Returns false if
            the stream is closed. */
        {
            PIOSTRUCT str = get_stream(args->WordP());
            if (str != NULL && isatty(str->device.ioDesc))
                return Make_arbitrary_precision(1);
            else return Make_arbitrary_precision(0);
        }

    case 33: /* sysconf. */
        return getSysConf(args);

        /* Filesys entries. */
    case 50: /* Set the file creation mask and return the old one. */
        {
            mode_t mode = get_C_ulong(DEREFWORDHANDLE(args));
            return Make_unsigned(umask(mode));
        }

    case 51: /* Create a hard link. */
        {
            char *old = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            char *newp = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(1));
            int err, res;
            res = link(old, newp);
            err = errno; /* Save the error result in case free changes it. */
            free(old);
            free(newp);
            if (res < 0) raise_syscall("link failed", err);
            return Make_arbitrary_precision(0);
        }

    case 52: /* Create a directory.  There is an OS-independent version in
            basicio which uses a default creation mode. */
        {
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            mode_t mode = get_C_long(DEREFHANDLE(args)->Get(1));
            int err, res;
            res = mkdir(name, mode);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            if (res < 0) raise_syscall("mkdir failed", err);
            return Make_arbitrary_precision(0);
        }

    case 53: /* Create a fifo. */
        {
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            mode_t mode = get_C_long(DEREFHANDLE(args)->Get(1));
            int err, res;
            res = mkfifo(name, mode);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            if (res < 0) raise_syscall("mkfifo failed", err);
            return Make_arbitrary_precision(0);
        }

    case 54: /* Create a symbolic link. */
        {
            char *old = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            char *newp = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(1));
            int err, res;
            res = symlink(old, newp);
            err = errno; /* Save the error result in case free changes it. */
            free(old);
            free(newp);
            if (res < 0) raise_syscall("link failed", err);
            return Make_arbitrary_precision(0);
        }

    case 55: /* Get information about a file. */
        {
            struct stat buf;
            int res, err;
            char *name = Poly_string_to_C_alloc(DEREFWORD(args));
            res = stat(name, &buf);
            err = errno;
            free(name);
            if (res < 0) raise_syscall("stat failed", err);
            return getStatInfo(&buf);
        }

    case 56: /* Get information about a symbolic link. */
        {
            struct stat buf;
            int res, err;
            char *name = Poly_string_to_C_alloc(DEREFWORD(args));
            res = lstat(name, &buf);
            err = errno;
            free(name);
            if (res < 0) raise_syscall("lstat failed", err);
            return getStatInfo(&buf);
        }

    case 57: /* Get information about an open file. */
        {
            struct stat buf;
            PIOSTRUCT strm = get_stream(args->WordP());
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (fstat(strm->device.ioDesc, &buf) < 0)
                raise_syscall("fstat failed", errno);
            return getStatInfo(&buf);
        }

    case 58: /* Test access rights to a file. */
        {
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            int amode = get_C_long(DEREFHANDLE(args)->Get(1));
            int res;
            res = access(name, amode);
            free(name);
            /* Return false if error, true if not.  It's not clear that
               this is correct since there are several reasons why we
               might get -1 as the result. */
            return Make_arbitrary_precision(res < 0 ? 0 : 1);
        }

    case 59: /* Change access rights. */
        {
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            mode_t mode = get_C_long(DEREFHANDLE(args)->Get(1));
            int err, res;
            res = chmod(name, mode);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            if (res < 0) raise_syscall("chmod failed", err);
            return Make_arbitrary_precision(0);
        }

    case 60: /* Change access rights on open file. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            mode_t mode = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (fchmod(strm->device.ioDesc, mode) < 0)
                raise_syscall("fchmod failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 61: /* Change owner and group. */
        {
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            uid_t uid = get_C_long(DEREFHANDLE(args)->Get(1));
            gid_t gid = get_C_long(DEREFHANDLE(args)->Get(2));
            int err, res;
            res = chown(name, uid, gid);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            if (res < 0) raise_syscall("chown failed", err);
            return Make_arbitrary_precision(0);
        }

    case 62: /* Change owner and group on open file. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            uid_t uid = get_C_long(DEREFHANDLE(args)->Get(1));
            gid_t gid = get_C_long(DEREFHANDLE(args)->Get(2));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (fchown(strm->device.ioDesc, uid, gid) < 0)
                raise_syscall("fchown failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 63: /* Set access and modification times.  We use utimes rather than utime
            since it allows us to be more accurate.  There's a similar function
            in basicio which sets both the access and modification times to the
            same time. */
        {
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            Handle hAccess = SAVE(DEREFHANDLE(args)->Get(1));
            Handle hMod = SAVE(DEREFHANDLE(args)->Get(2));
            struct timeval times[2];
            /* We have a value in microseconds.  We need to split
               it into seconds and microseconds.  N.B. The arguments to
               div_longc and rem_longc are in reverse order. */
            Handle hMillion = Make_arbitrary_precision(1000000);
            unsigned secsAccess =
                get_C_ulong(DEREFWORDHANDLE(div_longc(hMillion, hAccess)));
            unsigned usecsAccess =
                get_C_ulong(DEREFWORDHANDLE(rem_longc(hMillion, hAccess)));
            unsigned secsMod =
                get_C_ulong(DEREFWORDHANDLE(div_longc(hMillion, hMod)));
            unsigned usecsMod =
                get_C_ulong(DEREFWORDHANDLE(rem_longc(hMillion, hMod)));
            int err, res;
            times[0].tv_sec = secsAccess;
            times[0].tv_usec = usecsAccess;
            times[1].tv_sec = secsMod;
            times[1].tv_usec = usecsMod;
            res = utimes(name, times);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            if (res < 0) raise_syscall("utimes failed", err);
            return Make_arbitrary_precision(0);
        }

    case 64: /* Set access and modification times to the current time.  This could be
            defined in terms of the previous call and Time.now but it could result
            in an error due to rounding.  This is probably safer. */
        {
            char *name = Poly_string_to_C_alloc(DEREFWORD(args));
            int err, res;
            res = utimes(name, 0);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            if (res < 0) raise_syscall("utimes failed", err);
            return Make_arbitrary_precision(0);
        }

    case 65: /* Truncate an open file. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int size = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (ftruncate(strm->device.ioDesc, size) < 0)
                raise_syscall("ftruncate failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 66: /* Get the configured limits for a file. */
        {
            /* Look up the variable. May raise an exception. */
            int nvar = findPathVar(DEREFHANDLE(args)->Get(1));
            char *name = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));
            int err, res;
            /* Set errno to zero.  If there is no limit pathconf returns -1 but
               does not change errno.  */
            errno = 0;
            res = pathconf(name, nvar);
            err = errno; /* Save the error result in case free changes it. */
            free(name);
            /* We return -1 as a valid result indicating no limit. */
            if (res < 0 && err != 0) raise_syscall("pathconf failed", err);
            return Make_arbitrary_precision(res);
        }

    case 67: /* Get the configured limits for an open file. */
        {
            /* Look up the variable. May raise an exception. */
            int nvar = findPathVar(DEREFHANDLE(args)->Get(1));
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int res;
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            errno = 0; /* Unchanged if there is no limit. */
            res = fpathconf(strm->device.ioDesc, nvar);
            if (res < 0 && errno != 0) raise_syscall("fpathconf failed", errno);
            return Make_arbitrary_precision(res);
        }

        /* Password and group entries. */
    case 100: /* Get Password entry by name. */
        {
            char pwName[200];
            int length;
            struct passwd *pw;
            length = Poly_string_to_C(DEREFWORD(args), pwName, 200);
            if (length > 200)
                raise_syscall("Password name too long", ENAMETOOLONG);
            pw = getpwnam(pwName);
            if (pw == NULL)
                raise_syscall("Password entry not found", ENOENT);
            return makePasswordEntry(pw);
        }

    case 101: /* Get password entry by uid. */
        {
            int uid = get_C_long(DEREFWORDHANDLE(args));
            struct passwd *pw = getpwuid(uid);
            if (pw == NULL)
                raise_syscall("Password entry not found", ENOENT);
            return makePasswordEntry(pw);
        }

    case 102: /* Get group entry by name. */
        {
            struct group *grp;
            char grpName[200];
            int length;
            length = Poly_string_to_C(DEREFWORD(args), grpName, 200);
            if (length > 200)
                raise_syscall("Group name too long", ENAMETOOLONG);
            grp = getgrnam(grpName);
            if (grp == NULL)
                raise_syscall("Group entry not found", ENOENT);
            return makeGroupEntry(grp);
        }
        
    case 103: /* Get group entry by gid. */
        {
            int gid = get_C_long(DEREFWORDHANDLE(args));
            struct group *grp = getgrgid(gid);
            if (grp == NULL)
                raise_syscall("Group entry not found", ENOENT);
            return makeGroupEntry(grp);
        }

        /* IO Entries. */
    case 110: /* Create a pipe. */
        {
            int filedes[2];
            Handle strRead = make_stream_entry();
            Handle strWrite = make_stream_entry();
            Handle result;
            PIOSTRUCT instrm, outstrm;
            if (pipe(filedes) < 0) raise_syscall("pipe failed", errno);
            instrm = &basic_io_vector[STREAMID(strRead)];
            outstrm = &basic_io_vector[STREAMID(strWrite)];
            instrm->device.ioDesc = filedes[0];
            instrm->ioBits = IO_BIT_OPEN | IO_BIT_READ;
            outstrm->device.ioDesc = filedes[1];
            outstrm->ioBits = IO_BIT_OPEN | IO_BIT_WRITE;
            result = ALLOC(2);
            DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(strRead));
            DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(strWrite));
            return result;
        }

    case 111: /* Duplicate a file descriptor. */
        {
            PIOSTRUCT str = get_stream(args->WordP());
            PIOSTRUCT newstr;
            Handle strToken = make_stream_entry();
            int fd;
            if (str == NULL) raise_syscall("Stream is closed", EBADF);
            fd = dup(str->device.ioDesc);
            if (fd < 0) raise_syscall("dup failed", errno);
            newstr = &basic_io_vector[STREAMID(strToken)];
            newstr->device.ioDesc = fd;
            /* I'm assuming that we're not going to put any other
               status information in the bits. */
            newstr->ioBits = str->ioBits;
            return strToken;
        }

    case 112: /* Duplicate a file descriptor to a given entry. */
        {
            PIOSTRUCT old = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            PIOSTRUCT newp = get_stream(DEREFHANDLE(args)->Get(1).AsObjPtr());
            /* The "newp" entry must be an open entry in our io table.
               It may, though, be an entry added through wordToFD
               (basicio call 31) which may not refer to a valid
               descriptor. */
            if (old == NULL || newp == NULL)
                raise_syscall("Stream is closed", EBADF);
            if (dup2(old->device.ioDesc, newp->device.ioDesc) < 0)
                raise_syscall("dup2 failed", errno);
            newp->ioBits = old->ioBits;
            return Make_arbitrary_precision(0);
        }

    case 113: /* Duplicate a file descriptor to an entry equal to or greater
             than the given value. */
        {
            PIOSTRUCT old = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            PIOSTRUCT base = get_stream(DEREFHANDLE(args)->Get(1).AsObjPtr());
            int newfd;
            Handle strToken = make_stream_entry();
            PIOSTRUCT newstr;
            /* The "base" entry must be an open entry in our io table.
               It may, though, be an entry added through wordToFD
               (basicio call 31) which may not refer to a valid
               descriptor. */
            if (old == NULL || base == NULL)
                raise_syscall("Stream is closed", EBADF);
            newfd = fcntl(old->device.ioDesc, F_DUPFD, base->device.ioDesc);
            if (newfd < 0) raise_syscall("dup2 failed", errno);
            newstr = &basic_io_vector[STREAMID(strToken)];
            newstr->device.ioDesc = newfd;
            /* I'm assuming that we're not going to put any other
               status information in the bits. */
            newstr->ioBits = old->ioBits;
            return strToken;
        }

    case 114: /* Get the file descriptor flags. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            int res;
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            res = fcntl(strm->device.ioDesc, F_GETFD);
            if (res < 0) raise_syscall("fcntl failed", errno);
            return Make_arbitrary_precision(res);
        }

    case 115: /* Set the file descriptor flags. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int flags = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (fcntl(strm->device.ioDesc, F_SETFD, flags) < 0)
                raise_syscall("fcntl failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 116: /* Get the file status and access flags. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            int res;
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            res = fcntl(strm->device.ioDesc, F_GETFL);
            if (res < 0) raise_syscall("fcntl failed", errno);
            return Make_arbitrary_precision(res);
        }

    case 117: /* Set the file status and access flags. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int flags = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (fcntl(strm->device.ioDesc, F_SETFL, flags) < 0)
                raise_syscall("fcntl failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 118: /* Seek to a position on the stream. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            long position = get_C_long(DEREFHANDLE(args)->Get(1));
            int whence = get_C_long(DEREFHANDLE(args)->Get(2));
            long newpos;
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            newpos = lseek(strm->device.ioDesc, position, whence);
            if (newpos < 0) raise_syscall("lseek failed", errno);
            return Make_arbitrary_precision(newpos);
        }

    case 119: /* Synchronise file contents. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (fsync(strm->device.ioDesc) < 0) raise_syscall("fsync failed", errno);
            return Make_arbitrary_precision(0);
        }

    case 120: /* get lock */
        return lockCommand(F_GETLK, args);

    case 121: /* set lock */
        return lockCommand(F_SETLK, args);

    case 122: /* wait for lock */
        /* TODO: This may well block the whole process.  We should look at the
           result and retry if need be. */
        return lockCommand(F_SETLKW, args);

        /* TTY entries. */
    case 150: /* Get attributes. */
        return getTTYattrs(args);

    case 151: /* Set attributes. */
        return setTTYattrs(args);

    case 152: /* Send a break. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int duration = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (tcsendbreak(strm->device.ioDesc, duration) < 0)
                raise_syscall("tcsendbreak failed", errno);
            return Make_arbitrary_precision(0);
        }
    
    case 153: /* Wait for output to drain. */
        {
            /* TODO: This will block the process.  It really needs to
               check whether the stream has drained and run another
               process until it has. */
            PIOSTRUCT strm = get_stream(args->WordP());
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (tcdrain(strm->device.ioDesc) < 0)
                raise_syscall("tcdrain failed", errno);
            return Make_arbitrary_precision(0);
        }
    
    case 154: /* Flush terminal stream. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int qs = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (tcflush(strm->device.ioDesc, qs) < 0)
                raise_syscall("tcflush failed", errno);
            return Make_arbitrary_precision(0);
        }
    
    case 155: /* Flow control. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int action = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (tcflow(strm->device.ioDesc, action) < 0)
                raise_syscall("tcflow failed", errno);
            return Make_arbitrary_precision(0);
        }
    
    case 156: /* Get process group. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            pid_t pid;
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            pid = tcgetpgrp(strm->device.ioDesc);
            if (pid < 0) raise_syscall("tcgetpgrp failed", errno);
            return Make_arbitrary_precision(pid);
        }
    
    case 157: /* Set process group. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            pid_t pid = get_C_long(DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall("Stream is closed", EBADF);
            if (tcsetpgrp(strm->device.ioDesc, pid) < 0)
                raise_syscall("tcsetpgrp failed", errno);
            return Make_arbitrary_precision(0);
        }
    
    default:
        {
            char msg[100];
            sprintf(msg, "Unknown unix-specific function: %d", c);
            raise_exception_string(EXC_Fail, msg);
        }
    }
}

Handle waitForProcess(Handle args)
/* Get result status of a child process. */
{
TryAgain:
    int kind = get_C_long(DEREFHANDLE(args)->Get(0));
    int pid = get_C_long(DEREFHANDLE(args)->Get(1));
    int callFlags = get_C_long(DEREFHANDLE(args)->Get(2));
    int flags = callFlags | WNOHANG; // Add in WNOHANG so we never block.
    pid_t pres = 0;
    int status;
    switch (kind)
    {
    case 0: /* Wait for any child. */
        pres = waitpid(-1, &status, flags);
        break;
    case 1: /* Wait for specific process. */
        pres = waitpid(pid, &status, flags);
        break;
    case 2: /* Wait for any in current process group. */
        pres = waitpid(0, &status, flags);
        break;
    case 3: /* Wait for child in given process group */
        pres = waitpid(-pid, &status, flags);
        break;
    }
    if (pres < 0)
    {
        if (errno == EINTR)
            goto TryAgain;
        else
            raise_syscall("wait failed", errno);
    }
    /* If the caller did not specify WNOHANG but there
       wasn't a child process waiting we have to block
       and come back here later. */
    if (pres == 0 && !(callFlags & WNOHANG))
        processes->block_and_restart(-1, 0, POLY_SYS_os_specific);

    /* Construct the result tuple. */
    {
        Handle result, pidHandle, resHandle;
        pidHandle = Make_arbitrary_precision(pres);
        resHandle = Make_arbitrary_precision(status);

        result = ALLOC(2);
        DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(pidHandle));
        DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(resHandle));
        return result;
    }
}

static Handle makePasswordEntry(struct passwd *pw)
/* Return a password entry. */
{
    Handle nameHandle, uidHandle, gidHandle, homeHandle, shellHandle, result;
    nameHandle = SAVE(C_string_to_Poly(pw->pw_name));
    uidHandle = Make_arbitrary_precision(pw->pw_uid);
    gidHandle = Make_arbitrary_precision(pw->pw_gid);
    homeHandle = SAVE(C_string_to_Poly(pw->pw_dir));
    shellHandle = SAVE(C_string_to_Poly(pw->pw_shell));
    result = ALLOC(5);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(nameHandle));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(uidHandle));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(gidHandle));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(homeHandle));
    DEREFHANDLE(result)->Set(4, DEREFWORDHANDLE(shellHandle));
    return result;
}

static Handle makeGroupEntry(struct group *grp)
{
    Handle nameHandle, gidHandle, membersHandle, result;
    int i;
    char **p;
    nameHandle = SAVE(C_string_to_Poly(grp->gr_name));
    gidHandle = Make_arbitrary_precision(grp->gr_gid);
    /* Group members. */
    for (i=0, p = grp->gr_mem; *p != NULL; p++, i++);
    membersHandle = convert_string_list(i, grp->gr_mem);
    result = ALLOC(3);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(nameHandle));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(gidHandle));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(membersHandle));
    return result;
}

/* Make a cons cell for a pair of strings. */
// Doesn't currently reset the save vec so it's only safe for a small number
// of cells.
static void makeStringPairList(Handle &list, char *s1, char *s2)
{
    Handle nameHandle, valueHandle, pairHandle, next;
    /* This has to be done carefully to ensure we don't throw anything
       away if we garbage-collect and also to ensure that each object is
       fully initialised before the next object is created. */
    /* Make the strings. */
    nameHandle = SAVE(C_string_to_Poly(s1));
    valueHandle = SAVE(C_string_to_Poly(s2));
    /* Make the pair. */
    pairHandle = ALLOC(2);
    DEREFHANDLE(pairHandle)->Set(0, DEREFWORDHANDLE(nameHandle));
    DEREFHANDLE(pairHandle)->Set(1, DEREFWORDHANDLE(valueHandle));
    /* Make the cons cell. */
    next  = ALLOC(SIZEOF(ML_Cons_Cell));
    DEREFLISTHANDLE(next)->h = DEREFWORDHANDLE(pairHandle); 
    DEREFLISTHANDLE(next)->t = DEREFLISTHANDLE(list);
    list = SAVE(DEREFHANDLE(next));
}

/* Return the uname information.  */
static Handle getUname(void)
{
#ifdef HAVE_SYS_UTSNAME_H
    struct utsname name;
    Handle list = SAVE(ListNull);
    if (uname(&name) < 0) raise_syscall("uname failed", errno);
    makeStringPairList(list, "sysname", name.sysname);
    makeStringPairList(list, "nodename", name.nodename);
    makeStringPairList(list, "release", name.release);
    makeStringPairList(list, "version", name.version);
    makeStringPairList(list, "machine", name.machine);
    return list;
#else
    raise_syscall("uname not available on this machine", errno);
#endif
}

/* Return the contents of a stat buffer. */
static Handle getStatInfo(struct stat *buf)
{
    int kind;
    Handle result, modeHandle, kindHandle, inoHandle, devHandle, linkHandle;
    Handle uidHandle, gidHandle, sizeHandle, atimeHandle, mtimeHandle, ctimeHandle;
    /* Get the protection mode, masking off the file type info. */
    modeHandle =
        Make_unsigned(buf->st_mode & (S_IRWXU|S_IRWXG|S_IRWXO|S_ISUID|S_ISGID));
    if (S_ISDIR(buf->st_mode)) kind = 1;
    else if (S_ISCHR(buf->st_mode)) kind = 2;
    else if (S_ISBLK(buf->st_mode)) kind = 3;
    else if (S_ISFIFO(buf->st_mode)) kind = 4;
    else if ((buf->st_mode & S_IFMT) == S_IFLNK) kind = 5;
    else if ((buf->st_mode & S_IFMT) == S_IFSOCK) kind = 6;
    else /* Regular. */ kind = 0;
    kindHandle = Make_unsigned(kind);
    inoHandle = Make_unsigned(buf->st_ino);
    devHandle = Make_unsigned(buf->st_dev);
    linkHandle = Make_unsigned(buf->st_nlink);
    uidHandle = Make_unsigned(buf->st_uid);
    gidHandle = Make_unsigned(buf->st_gid);
    sizeHandle = Make_unsigned(buf->st_size);
    /* TODO: The only really standard time fields give the seconds part.  There
       are various extensions which would give us microseconds or nanoseconds. */
    atimeHandle = Make_arb_from_pair_scaled(buf->st_atime, 0, 1000000);
    mtimeHandle = Make_arb_from_pair_scaled(buf->st_mtime, 0, 1000000);
    ctimeHandle = Make_arb_from_pair_scaled(buf->st_ctime, 0, 1000000);
    result = ALLOC(11);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(modeHandle));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(kindHandle));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(inoHandle));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(devHandle));
    DEREFHANDLE(result)->Set(4, DEREFWORDHANDLE(linkHandle));
    DEREFHANDLE(result)->Set(5, DEREFWORDHANDLE(uidHandle));
    DEREFHANDLE(result)->Set(6, DEREFWORDHANDLE(gidHandle));
    DEREFHANDLE(result)->Set(7, DEREFWORDHANDLE(sizeHandle));
    DEREFHANDLE(result)->Set(8, DEREFWORDHANDLE(atimeHandle));
    DEREFHANDLE(result)->Set(9, DEREFWORDHANDLE(mtimeHandle));
    DEREFHANDLE(result)->Set(10, DEREFWORDHANDLE(ctimeHandle));
    return result;
}



static Handle getTTYattrs(Handle args)
{
    PIOSTRUCT strm = get_stream(args->WordP());
    struct termios tios;
    speed_t ispeed, ospeed;
    Handle ifHandle, ofHandle, cfHandle, lfHandle, ccHandle;
    Handle isHandle, osHandle, result;
    if (strm == NULL) raise_syscall("Stream is closed", EBADF);
    if (tcgetattr(strm->device.ioDesc, &tios) < 0)
        raise_syscall("tcgetattr failed", errno);
    /* Extract the speed entries. */
    ospeed = cfgetospeed(&tios);
    ispeed = cfgetispeed(&tios);
    /* Set the speed entries to zero.  In Solaris, at least, the speed is
       encoded in the flags and we don't want any confusion.  The order of
       these functions is significant.  */
    cfsetospeed(&tios, B0);
    cfsetispeed(&tios, B0);
    /* Convert the values to ML representation. */
    ifHandle = Make_unsigned(tios.c_iflag);
    ofHandle = Make_unsigned(tios.c_oflag);
    cfHandle = Make_unsigned(tios.c_cflag);
    lfHandle = Make_unsigned(tios.c_lflag);
    /* The cc vector is treated as a string. */
    ccHandle = SAVE(Buffer_to_Poly((const char *)tios.c_cc, NCCS));
    isHandle = Make_unsigned(ispeed);
    osHandle = Make_unsigned(ospeed);
    /* We can now create the result tuple. */
    result = ALLOC(7);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(ifHandle));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(ofHandle));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(cfHandle));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(lfHandle));
    DEREFHANDLE(result)->Set(4, DEREFWORDHANDLE(ccHandle));
    DEREFHANDLE(result)->Set(5, DEREFWORDHANDLE(isHandle));
    DEREFHANDLE(result)->Set(6, DEREFWORDHANDLE(osHandle));
    return result;
}

/* Assemble the tios structure from the arguments and set the TTY attributes. */
static Handle setTTYattrs(Handle args)
{
    PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
    int actions = get_C_long(DEREFHANDLE(args)->Get(1));
    struct termios tios;
    speed_t ispeed, ospeed;
    if (strm == NULL) raise_syscall("Stream is closed", EBADF);
    /* Make sure anything unset is zero.  It might be better to call
       tcgetattr instead. */
    memset(&tios, 0, sizeof(tios));
    tios.c_iflag = get_C_ulong(DEREFHANDLE(args)->Get(2));
    tios.c_oflag = get_C_ulong(DEREFHANDLE(args)->Get(3));
    tios.c_cflag = get_C_ulong(DEREFHANDLE(args)->Get(4));
    tios.c_lflag = get_C_ulong(DEREFHANDLE(args)->Get(5));
    /* The cc vector should be a string of exactly NCCS characters.  It
       may well contain nulls so we can't use Poly_string_to_C to copy it. */
    PolyWord ccv = DEREFHANDLE(args)->Get(6);
    if (ccv.IsTagged()) // Just to check.
        raise_syscall("Incorrect cc vector", EINVAL);
    PolyStringObject * ccvs = (PolyStringObject *)ccv.AsObjPtr();
    if (ccvs->length != NCCS) // Just to check. */
        raise_syscall("Incorrect cc vector", EINVAL);
    memcpy(tios.c_cc, ccvs->chars, NCCS);
    ispeed = get_C_ulong(DEREFHANDLE(args)->Get(7));
    ospeed = get_C_ulong(DEREFHANDLE(args)->Get(8));
    if (cfsetispeed(&tios, ispeed) < 0)
        raise_syscall("cfsetispeed failed", errno);
    if (cfsetospeed(&tios, ospeed) < 0)
        raise_syscall("cfsetospeed failed", errno);
    /* Now it's all set we can call tcsetattr to do the work. */
    if (tcsetattr(strm->device.ioDesc, actions, &tios) < 0)
        raise_syscall("tcsetattr failed", errno);
    return Make_arbitrary_precision(0);
}

/* Lock/unlock/test file locks.  Returns the, possibly modified, argument structure. */
static Handle lockCommand(int cmd, Handle args)
{
    PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
    struct flock lock;
    Handle result, typeHandle, whenceHandle, startHandle, lenHandle, pidHandle;
    memset(&lock, 0, sizeof(lock)); /* Make sure unused fields are zero. */
    if (strm == NULL) raise_syscall("Stream is closed", EBADF);
    lock.l_type = get_C_long(DEREFHANDLE(args)->Get(1));
    lock.l_whence = get_C_long(DEREFHANDLE(args)->Get(2));
    lock.l_start = get_C_long(DEREFHANDLE(args)->Get(3));
    lock.l_len = get_C_long(DEREFHANDLE(args)->Get(4));
    lock.l_pid = get_C_long(DEREFHANDLE(args)->Get(5));
    if (fcntl(strm->device.ioDesc, cmd, &lock) < 0) 
        raise_syscall("fcntl failed", errno);
    /* Construct the result. */
    typeHandle = Make_arbitrary_precision(lock.l_type);
    whenceHandle = Make_arbitrary_precision(lock.l_whence);
    startHandle = Make_arbitrary_precision(lock.l_start);
    lenHandle = Make_arbitrary_precision(lock.l_len);
    pidHandle = Make_arbitrary_precision(lock.l_pid);
    result = ALLOC(5);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(typeHandle));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(whenceHandle));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(startHandle));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(lenHandle));
    DEREFHANDLE(result)->Set(4, DEREFWORDHANDLE(pidHandle));
    return result;
}


/* This table maps string arguments for sysconf into the corresponding constants. */
/* These are highly OS dependent.  It has been configured on Solaris 2.8, Linux Redhat 5.2
   and FreeBSD 3.4. */
static struct {
    char *saName;
    int saVal;
} sysArgTable[] =
{
    { "_SC_ARG_MAX",        _SC_ARG_MAX },
    { "_SC_CHILD_MAX",      _SC_CHILD_MAX },
    { "_SC_CLK_TCK",        _SC_CLK_TCK },
    { "_SC_NGROUPS_MAX",        _SC_NGROUPS_MAX },
    { "_SC_OPEN_MAX",       _SC_OPEN_MAX },
    { "_SC_JOB_CONTROL",        _SC_JOB_CONTROL },
    { "_SC_SAVED_IDS",      _SC_SAVED_IDS },
    { "_SC_VERSION",        _SC_VERSION },
#ifdef _SC_PASS_MAX
    { "_SC_PASS_MAX",       _SC_PASS_MAX },
#endif
#ifdef _SC_LOGNAME_MAX
    { "_SC_LOGNAME_MAX",        _SC_LOGNAME_MAX },
#endif
#ifdef _SC_PAGESIZE
    { "_SC_PAGESIZE",       _SC_PAGESIZE },
#endif
#ifdef _SC_XOPEN_VERSION
    { "_SC_XOPEN_VERSION",      _SC_XOPEN_VERSION },
#endif
#ifdef SC_NPROCESSORS_CONF
    { "_SC_NPROCESSORS_CONF",   _SC_NPROCESSORS_CONF },
#endif
#ifdef _SC_NPROCESSORS_ONLN
    { "_SC_NPROCESSORS_ONLN",   _SC_NPROCESSORS_ONLN },
#endif
#ifdef _SC_STREAM_MAX
    { "_SC_STREAM_MAX",     _SC_STREAM_MAX },
#endif
#ifdef _SC_TZNAME_MAX
    { "_SC_TZNAME_MAX",     _SC_TZNAME_MAX },
#endif
#ifdef _SC_AIO_LISTIO_MAX
    { "_SC_AIO_LISTIO_MAX",     _SC_AIO_LISTIO_MAX },
#endif
#ifdef _SC_AIO_MAX
    { "_SC_AIO_MAX",        _SC_AIO_MAX },
#endif
#ifdef _SC_AIO_PRIO_DELTA_MAX
    { "_SC_AIO_PRIO_DELTA_MAX", _SC_AIO_PRIO_DELTA_MAX },
#endif
#ifdef _SC_ASYNCHRONOUS_IO
    { "_SC_ASYNCHRONOUS_IO",    _SC_ASYNCHRONOUS_IO },
#endif
#ifdef _SC_DELAYTIMER_MAX
    { "_SC_DELAYTIMER_MAX",     _SC_DELAYTIMER_MAX },
#endif
#ifdef _SC_FSYNC
    { "_SC_FSYNC",          _SC_FSYNC },
#endif
#ifdef _SC_MAPPED_FILES
    { "_SC_MAPPED_FILES",       _SC_MAPPED_FILES },
#endif
#ifdef _SC_MEMLOCK
    { "_SC_MEMLOCK",        _SC_MEMLOCK },
#endif
#ifdef _SC_MEMLOCK_RANGE
    { "_SC_MEMLOCK_RANGE",      _SC_MEMLOCK_RANGE },
#endif
#ifdef _SC_MEMORY_PROTECTION
    { "_SC_MEMORY_PROTECTION",  _SC_MEMORY_PROTECTION },
#endif
#ifdef _SC_MESSAGE_PASSING
    { "_SC_MESSAGE_PASSING",    _SC_MESSAGE_PASSING },
#endif
#ifdef _SC_MQ_OPEN_MAX
    { "_SC_MQ_OPEN_MAX",        _SC_MQ_OPEN_MAX },
#endif
#ifdef _SC_MQ_PRIO_MAX
    { "_SC_MQ_PRIO_MAX",        _SC_MQ_PRIO_MAX },
#endif
#ifdef _SC_PRIORITIZED_IO
    { "_SC_PRIORITIZED_IO",     _SC_PRIORITIZED_IO },
#endif
#ifdef _SC_PRIORITY_SCHEDULING
    { "_SC_PRIORITY_SCHEDULING",    _SC_PRIORITY_SCHEDULING },
#endif
#ifdef _SC_REALTIME_SIGNALS
    { "_SC_REALTIME_SIGNALS",   _SC_REALTIME_SIGNALS },
#endif
#ifdef _SC_RTSIG_MAX
    { "_SC_RTSIG_MAX",      _SC_RTSIG_MAX },
#endif
#ifdef _SC_SEMAPHORES
    { "_SC_SEMAPHORES",     _SC_SEMAPHORES },
#endif
#ifdef _SC_SEM_NSEMS_MAX
    { "_SC_SEM_NSEMS_MAX",      _SC_SEM_NSEMS_MAX },
#endif
#ifdef _SC_SEM_VALUE_MAX
    { "_SC_SEM_VALUE_MAX",      _SC_SEM_VALUE_MAX },
#endif
#ifdef _SC_SHARED_MEMORY_OBJECTS
    { "_SC_SHARED_MEMORY_OBJECTS",  _SC_SHARED_MEMORY_OBJECTS },
#endif
#ifdef _SC_SIGQUEUE_MAX
    { "_SC_SIGQUEUE_MAX",       _SC_SIGQUEUE_MAX },
#endif
#ifdef _SC_SIGRT_MIN
    { "_SC_SIGRT_MIN",      _SC_SIGRT_MIN },
#endif
#ifdef _SC_SIGRT_MAX
    { "_SC_SIGRT_MAX",      _SC_SIGRT_MAX },
#endif
#ifdef _SC_SYNCHRONIZED_IO
    { "_SC_SYNCHRONIZED_IO",    _SC_SYNCHRONIZED_IO },
#endif
#ifdef _SC_TIMERS
    { "_SC_TIMERS",         _SC_TIMERS },
#endif
#ifdef _SC_TIMER_MAX
    { "_SC_TIMER_MAX",      _SC_TIMER_MAX },
#endif
#ifdef _SC_2_C_BIND
    { "_SC_2_C_BIND",       _SC_2_C_BIND },
#endif
#ifdef _SC_2_C_DEV
    { "_SC_2_C_DEV",        _SC_2_C_DEV },
#endif
#ifdef _SC_2_C_VERSION
    { "_SC_2_C_VERSION",        _SC_2_C_VERSION },
#endif
#ifdef _SC_2_FORT_DEV
    { "_SC_2_FORT_DEV",     _SC_2_FORT_DEV },
#endif
#ifdef _SC_2_FORT_RUN
    { "_SC_2_FORT_RUN",     _SC_2_FORT_RUN },
#endif
#ifdef _SC_2_LOCALEDEF
    { "_SC_2_LOCALEDEF",        _SC_2_LOCALEDEF },
#endif
#ifdef _SC_2_SW_DEV
    { "_SC_2_SW_DEV",       _SC_2_SW_DEV },
#endif
#ifdef _SC_2_UPE
    { "_SC_2_UPE",          _SC_2_UPE },
#endif
#ifdef _SC_2_VERSION
    { "_SC_2_VERSION",      _SC_2_VERSION },
#endif
#ifdef _SC_BC_BASE_MAX
    { "_SC_BC_BASE_MAX",        _SC_BC_BASE_MAX },
#endif
#ifdef _SC_BC_DIM_MAX
    { "_SC_BC_DIM_MAX",     _SC_BC_DIM_MAX },
#endif
#ifdef _SC_BC_SCALE_MAX
    { "_SC_BC_SCALE_MAX",       _SC_BC_SCALE_MAX },
#endif
#ifdef _SC_BC_STRING_MAX
    { "_SC_BC_STRING_MAX",      _SC_BC_STRING_MAX },
#endif
#ifdef _SC_COLL_WEIGHTS_MAX
    { "_SC_COLL_WEIGHTS_MAX",   _SC_COLL_WEIGHTS_MAX },
#endif
#ifdef _SC_EXPR_NEST_MAX
    { "_SC_EXPR_NEST_MAX",      _SC_EXPR_NEST_MAX },
#endif
#ifdef _SC_LINE_MAX
    { "_SC_LINE_MAX",       _SC_LINE_MAX },
#endif
#ifdef _SC_RE_DUP_MAX
   { "_SC_RE_DUP_MAX",     _SC_RE_DUP_MAX },
#endif
#ifdef _SC_XOPEN_CRYPT
    { "_SC_XOPEN_CRYPT",        _SC_XOPEN_CRYPT },
#endif
#ifdef _SC_XOPEN_ENH_I18N
    { "_SC_XOPEN_ENH_I18N",     _SC_XOPEN_ENH_I18N },
#endif
#ifdef _SC_XOPEN_SHM
    { "_SC_XOPEN_SHM",      _SC_XOPEN_SHM },
#endif
#ifdef _SC_2_CHAR_TERM
    { "_SC_2_CHAR_TERM",        _SC_2_CHAR_TERM },
#endif
#ifdef _SC_XOPEN_XCU_VERSION
    { "_SC_XOPEN_XCU_VERSION",  _SC_XOPEN_XCU_VERSION },
#endif
#ifdef _SC_ATEXIT_MAX
    { "_SC_ATEXIT_MAX",     _SC_ATEXIT_MAX },
#endif
#ifdef _SC_IOV_MAX
    { "_SC_IOV_MAX",        _SC_IOV_MAX },
#endif
#ifdef _SC_XOPEN_UNIX
    { "_SC_XOPEN_UNIX",     _SC_XOPEN_UNIX },
#endif
#ifdef _SC_PAGE_SIZE
    { "_SC_PAGE_SIZE",      _SC_PAGE_SIZE },
#endif
#ifdef _SC_T_IOV_MAX
    { "_SC_T_IOV_MAX",      _SC_T_IOV_MAX },
#endif
#ifdef _SC_PHYS_PAGES
    { "_SC_PHYS_PAGES",     _SC_PHYS_PAGES },
#endif
#ifdef _SC_AVPHYS_PAGES
    { "_SC_AVPHYS_PAGES",       _SC_AVPHYS_PAGES },
#endif
#ifdef _SC_COHER_BLKSZ
    { "_SC_COHER_BLKSZ",        _SC_COHER_BLKSZ },
#endif
#ifdef _SC_SPLIT_CACHE
    { "_SC_SPLIT_CACHE",        _SC_SPLIT_CACHE },
#endif
#ifdef _SC_ICACHE_SZ
    { "_SC_ICACHE_SZ",      _SC_ICACHE_SZ },
#endif
#ifdef _SC_DCACHE_SZ
    { "_SC_DCACHE_SZ",      _SC_DCACHE_SZ },
#endif
#ifdef _SC_ICACHE_LINESZ
    { "_SC_ICACHE_LINESZ",      _SC_ICACHE_LINESZ },
#endif
#ifdef _SC_DCACHE_LINESZ
    { "_SC_DCACHE_LINESZ",      _SC_DCACHE_LINESZ },
#endif
#ifdef _SC_ICACHE_BLKSZ
    { "_SC_ICACHE_BLKSZ",       _SC_ICACHE_BLKSZ },
#endif
#ifdef _SC_DCACHE_BLKSZ
    { "_SC_DCACHE_BLKSZ",       _SC_DCACHE_BLKSZ },
#endif
#ifdef _SC_DCACHE_TBLKSZ
    { "_SC_DCACHE_TBLKSZ",      _SC_DCACHE_TBLKSZ },
#endif
#ifdef _SC_ICACHE_ASSOC
    { "_SC_ICACHE_ASSOC",       _SC_ICACHE_ASSOC },
#endif
#ifdef _SC_DCACHE_ASSOC
    { "_SC_DCACHE_ASSOC",       _SC_DCACHE_ASSOC },
#endif
#ifdef _SC_MAXPID
    { "_SC_MAXPID",         _SC_MAXPID },
#endif
#ifdef _SC_STACK_PROT
    { "_SC_STACK_PROT",     _SC_STACK_PROT },
#endif
#ifdef _SC_THREAD_DESTRUCTOR_ITERATIONS
    { "_SC_THREAD_DESTRUCTOR_ITERATIONS",   _SC_THREAD_DESTRUCTOR_ITERATIONS },
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
    { "_SC_GETGR_R_SIZE_MAX",   _SC_GETGR_R_SIZE_MAX },
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
    { "_SC_GETPW_R_SIZE_MAX",   _SC_GETPW_R_SIZE_MAX },
#endif
#ifdef _SC_LOGIN_NAME_MAX
    { "_SC_LOGIN_NAME_MAX",     _SC_LOGIN_NAME_MAX },
#endif
#ifdef _SC_THREAD_KEYS_MAX
    { "_SC_THREAD_KEYS_MAX",    _SC_THREAD_KEYS_MAX },
#endif
#ifdef _SC_THREAD_STACK_MI
    { "_SC_THREAD_STACK_MIN",   _SC_THREAD_STACK_MIN },
#endif
#ifdef _SC_THREAD_THREADS_MAX
    { "_SC_THREAD_THREADS_MAX", _SC_THREAD_THREADS_MAX },
#endif
#ifdef _SC_THREAD_ATTR_STACKADDR
    { "_SC_THREAD_ATTR_STACKADDR",  _SC_THREAD_ATTR_STACKADDR },
#endif
#ifdef _SC_THREAD_ATTR_STACKSIZE
    { "_SC_THREAD_ATTR_STACKSIZE",  _SC_THREAD_ATTR_STACKSIZE },
#endif
#ifdef _SC_THREAD_PRIORITY_SCHEDULING
    { "_SC_THREAD_PRIORITY_SCHEDULING", _SC_THREAD_PRIORITY_SCHEDULING },
#endif
#ifdef _SC_THREAD_PRIO_INHERIT
    { "_SC_THREAD_PRIO_INHERIT",    _SC_THREAD_PRIO_INHERIT },
#endif
#ifdef _SC_THREAD_PRIO_PROTECT
    { "_SC_THREAD_PRIO_PROTECT",    _SC_THREAD_PRIO_PROTECT },
#endif
#ifdef _SC_THREAD_PROCESS_SHARED
    { "_SC_THREAD_PROCESS_SHARED",  _SC_THREAD_PROCESS_SHARED },
#endif
#ifdef _SC_XOPEN_LEGACY
    { "_SC_XOPEN_LEGACY",       _SC_XOPEN_LEGACY },
#endif
#ifdef _SC_XOPEN_REALTIME
    { "_SC_XOPEN_REALTIME",     _SC_XOPEN_REALTIME },
#endif
#ifdef _SC_XOPEN_REALTIME_THREADS
    { "_SC_XOPEN_REALTIME_THREADS", _SC_XOPEN_REALTIME_THREADS },
#endif
#ifdef _SC_XBS5_ILP32_OFF32
    { "_SC_XBS5_ILP32_OFF32",   _SC_XBS5_ILP32_OFF32 },
#endif
#ifdef _SC_XBS5_ILP32_OFFBIG
    { "_SC_XBS5_ILP32_OFFBIG",  _SC_XBS5_ILP32_OFFBIG },
#endif
#ifdef _SC_XBS5_LP64_OFF64
    { "_SC_XBS5_LP64_OFF64",    _SC_XBS5_LP64_OFF64 },
#endif
#ifdef _SC_XBS5_LPBIG_OFFBIG
    { "_SC_XBS5_LPBIG_OFFBIG",  _SC_XBS5_LPBIG_OFFBIG },
#endif
#ifdef _SC_EQUIV_CLASS_MAX
    { "_SC_EQUIV_CLASS_MAX",    _SC_EQUIV_CLASS_MAX },
#endif
#ifdef _SC_CHARCLASS_NAME_MAX
    { "_SC_CHARCLASS_NAME_MAX", _SC_CHARCLASS_NAME_MAX },
#endif
#ifdef _SC_PII
    { "_SC_PII",            _SC_PII },
#endif
#ifdef _SC_PII_XTI
    { "_SC_PII_XTI",        _SC_PII_XTI },
#endif
#ifdef _SC_PII_SOCKET
    { "_SC_PII_SOCKET",     _SC_PII_SOCKET },
#endif
#ifdef _SC_PII_INTERNET
    { "_SC_PII_INTERNET",       _SC_PII_INTERNET },
#endif
#ifdef _SC_PII_OSI
    { "_SC_PII_OSI",        _SC_PII_OSI },
#endif
#ifdef _SC_POLL
    { "_SC_POLL",           _SC_POLL },
#endif
#ifdef _SC_SELECT
    { "_SC_SELECT",         _SC_SELECT },
#endif
#ifdef _SC_UIO_MAXIOV
    { "_SC_UIO_MAXIOV",     _SC_UIO_MAXIOV },
#endif
#ifdef _SC_PII_INTERNET_STREAM
    { "_SC_PII_INTERNET_STREAM",    _SC_PII_INTERNET_STREAM },
#endif
#ifdef _SC_PII_INTERNET_DGRAM
    { "_SC_PII_INTERNET_DGRAM", _SC_PII_INTERNET_DGRAM },
#endif
#ifdef _SC_PII_OSI_COTS
    { "_SC_PII_OSI_COTS",       _SC_PII_OSI_COTS },
#endif
#ifdef _SC_PII_OSI_CLTS
    { "_SC_PII_OSI_CLTS",       _SC_PII_OSI_CLTS },
#endif
#ifdef _SC_PII_OSI_M
    { "_SC_PII_OSI_M",      _SC_PII_OSI_M },
#endif
#ifdef _SC_T_IOV_MAX
    { "_SC_T_IOV_MAX",      _SC_T_IOV_MAX },
#endif
#ifdef _SC_THREADS
    { "_SC_THREADS",        _SC_THREADS },
#endif
#ifdef _SC_THREAD_SAFE_FUNCTIONS
    { "_SC_THREAD_SAFE_FUNCTIONS",  _SC_THREAD_SAFE_FUNCTIONS },
#endif
#ifdef _SC_TTY_NAME_MAX
    { "_SC_TTY_NAME_MAX",       _SC_TTY_NAME_MAX },
#endif
#ifdef _SC_XOPEN_XPG2
    { "_SC_XOPEN_XPG2",     _SC_XOPEN_XPG2 },
#endif
#ifdef _SC_XOPEN_XPG3
    { "_SC_XOPEN_XPG3",     _SC_XOPEN_XPG3 },
#endif
#ifdef _SC_XOPEN_XPG4
    { "_SC_XOPEN_XPG4",     _SC_XOPEN_XPG4 },
#endif
#ifdef _SC_CHAR_BIT
    { "_SC_CHAR_BIT",       _SC_CHAR_BIT },
#endif
#ifdef _SC_CHAR_MAX
    { "_SC_CHAR_MAX",       _SC_CHAR_MAX },
#endif
#ifdef _SC_CHAR_MIN
    { "_SC_CHAR_MIN",       _SC_CHAR_MIN },
#endif
#ifdef _SC_INT_MAX
    { "_SC_INT_MAX",        _SC_INT_MAX },
#endif
#ifdef _SC_INT_MIN
    { "_SC_INT_MIN",        _SC_INT_MIN },
#endif
#ifdef _SC_LONG_BIT
    { "_SC_LONG_BIT",       _SC_LONG_BIT },
#endif
#ifdef _SC_WORD_BIT
    { "_SC_WORD_BIT",       _SC_WORD_BIT },
#endif
#ifdef _SC_MB_LEN_MAX
    { "_SC_MB_LEN_MAX",     _SC_MB_LEN_MAX },
#endif
#ifdef _SC_NZERO
    { "_SC_NZERO",          _SC_NZERO },
#endif
#ifdef _SC_SSIZE_MAX
    { "_SC_SSIZE_MAX",      _SC_SSIZE_MAX },
#endif
#ifdef _SC_SCHAR_MAX
    { "_SC_SCHAR_MAX",      _SC_SCHAR_MAX },
#endif
#ifdef _SC_SCHAR_MIN
    { "_SC_SCHAR_MIN",      _SC_SCHAR_MIN },
#endif
#ifdef _SC_SHRT_MAX
    { "_SC_SHRT_MAX",       _SC_SHRT_MAX },
#endif
#ifdef _SC_SHRT_MIN
    { "_SC_SHRT_MIN",       _SC_SHRT_MIN },
#endif
#ifdef _SC_UCHAR_MAX
    { "_SC_UCHAR_MAX",      _SC_UCHAR_MAX },
#endif
#ifdef _SC_UINT_MAX
    { "_SC_UINT_MAX",       _SC_UINT_MAX },
#endif
#ifdef _SC_ULONG_MAX
    { "_SC_ULONG_MAX",      _SC_ULONG_MAX },
#endif
#ifdef _SC_USHRT_MAX
    { "_SC_USHRT_MAX",      _SC_USHRT_MAX },
#endif
#ifdef _SC_NL_ARGMAX
    { "_SC_NL_ARGMAX",      _SC_NL_ARGMAX },
#endif
#ifdef _SC_NL_LANGMAX
    { "_SC_NL_LANGMAX",     _SC_NL_LANGMAX },
#endif
#ifdef _SC_NL_MSGMAX
    { "_SC_NL_MSGMAX",      _SC_NL_MSGMAX },
#endif
#ifdef _SC_NL_NMAX
    { "_SC_NL_NMAX",        _SC_NL_NMAX },
#endif
#ifdef _SC_NL_SETMAX
    { "_SC_NL_SETMAX",      _SC_NL_SETMAX },
#endif
};

static Handle getSysConf(Handle args)
{
    char argName[200];
    int length;
    unsigned i;
    long res;
    length = Poly_string_to_C(DEREFWORD(args), argName, 200);
    if (length > 200) raise_syscall("Argument name too long", ENAMETOOLONG);

    for (i = 0; i < sizeof(sysArgTable)/sizeof(sysArgTable[0]); i++) {
        if (strcmp(argName, sysArgTable[i].saName) == 0) break;
        /* See if it matches without the _SC_ at the beginning. */
        if (strcmp(argName, sysArgTable[i].saName+4) == 0) break;
    }
    if (i == sizeof(sysArgTable)/sizeof(sysArgTable[0]))
        raise_syscall("sysconf argument not found", EINVAL);
    errno = 0; /* Sysconf may return -1 without updating errno. */
    res = sysconf(sysArgTable[i].saVal);
    if (res < 0) raise_syscall("sysconf failed", errno);
    return Make_arbitrary_precision(res);
}


static struct {
    char *pcName;
    int pcVal;
} pathConfTable[] =
{
    { "_PC_LINK_MAX",   _PC_LINK_MAX },
    { "_PC_MAX_CANON",  _PC_MAX_CANON },
    { "_PC_MAX_INPUT",  _PC_MAX_INPUT },
    { "_PC_NAME_MAX",   _PC_NAME_MAX },
    { "_PC_PATH_MAX",   _PC_PATH_MAX },
    { "_PC_PIPE_BUF",   _PC_PIPE_BUF },
    { "_PC_NO_TRUNC",   _PC_NO_TRUNC },
    { "_PC_VDISABLE",   _PC_VDISABLE },
    { "_PC_CHOWN_RESTRICTED",   _PC_CHOWN_RESTRICTED },
#ifdef _PC_ASYNC_IO
    { "_PC_ASYNC_IO",   _PC_ASYNC_IO },
#endif
#ifdef _PC_PRIO_IO
    { "_PC_PRIO_IO",    _PC_PRIO_IO },
#endif
#ifdef _PC_SYNC_IO
    { "_PC_SYNC_IO",    _PC_SYNC_IO },
#endif
#ifdef _PC_FILESIZEBITS
    { "_PC_FILESIZEBITS",   _PC_FILESIZEBITS },
#endif
#ifdef _PC_SOCK_MAXBUF
    { "_PC_SOCK_MAXBUF",    _PC_SOCK_MAXBUF },
#endif
};

/* Look up a path variable in the table. */
static int findPathVar(PolyWord ps)
{
    char argName[200];
    int length;
    unsigned i;
    length = Poly_string_to_C(ps, argName, 200);
    if (length > 200) raise_syscall("Argument name too long", ENAMETOOLONG);

    for (i = 0; i < sizeof(pathConfTable)/sizeof(pathConfTable[0]); i++) {
        if (strcmp(argName, pathConfTable[i].pcName) == 0)
            return pathConfTable[i].pcVal;
        /* See if it matches without the _PC_ at the beginning. */
        if (strcmp(argName, pathConfTable[i].pcName+4) == 0)
            return pathConfTable[i].pcVal;
    }
    raise_syscall("pathconf argument not found", EINVAL);
}

class UnixSpecific: public RtsModule
{
public:
    virtual void Init(void);
    virtual void Interrupt(int sig);
};

// Declare this.  It will be automatically added to the table.
static UnixSpecific unixModule;

void UnixSpecific::Interrupt(int /*signum*/)
{
    /* Check the alarm clock. */
    if (alarmclock.tv_sec != 0 || alarmclock.tv_usec != 0)
    {
        struct timeval tv;
        struct timezone tz;
        if (gettimeofday(&tv, &tz) != 0) return;
        /* If the current time is after the alarm clock we schedule
           an interrupt and clear the timer. */
        if (tv.tv_sec > alarmclock.tv_sec ||
           (tv.tv_sec == alarmclock.tv_sec && tv.tv_usec >= alarmclock.tv_usec))
        {
            alarmclock.tv_sec = alarmclock.tv_usec = 0;
            /* Simulate a SIGALRM. */
            addSigCount(SIGALRM);
        }
    }
}

void UnixSpecific::Init(void)
{
    struct sigaction sigcatch;
    /* Ignore SIGPIPE - return any errors as failure to write. */
    memset(&sigcatch, 0, sizeof(sigcatch));
    sigcatch.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sigcatch, NULL);
}