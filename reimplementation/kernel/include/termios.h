/* reimplementation/kernel/include/termios.h
 * Shadows base-nexs/kernel/include/termios.h for hosted builds.
 * Adds TCSANOW (missing from the kernel's minimal termios) and
 * defers to the real system <termios.h> on POSIX hosts.
 */

#ifndef KERNEL_TERMIOS_H
#define KERNEL_TERMIOS_H

#ifndef NEXS_BAREMETAL
/* On hosted platforms, pull the real POSIX termios. */
#include_next <termios.h>
#else
/* Baremetal: minimal subset used by nexs_line.c */
typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_lflag;
    tcflag_t c_cflag;
    cc_t     c_cc[NCCS];
};
#define ICRNL    0000400
#define IXON     0002000
#define BRKINT   0000002
#define ISTRIP   0000040
#define INPCK    0000020
#define ECHO     0000010
#define ICANON   0000002
#define IEXTEN   0100000
#define ISIG     0000001
#define CS8      0000060
#define VMIN     6
#define VTIME    5
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
#endif /* NEXS_BAREMETAL */

#endif /* KERNEL_TERMIOS_H */
