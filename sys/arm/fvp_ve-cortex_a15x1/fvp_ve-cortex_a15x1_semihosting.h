#ifndef SEMIHOSTING_H
#define SEMIHOSTING_H

#define SYS_WRITE0	4
#define SEMIHOSTING_SVC	0x123456	/* SVC comment field for semihosting */

int __semi_call(int id, ...);

#endif /* ! SEMIHOSTING_H */
