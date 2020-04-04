/*   SPDX-License-Identifier: GPL-2.0
 *
 */

// #include <sys/types.h>
#include <linux/ioctl.h>

#define FIQ_BUFFER_SIZE		(256 * 1024)

#define FIQ_IOC_MAGIC            'p'
#define FIQ_START		_IO(FIQ_IOC_MAGIC, 0xb0)
#define FIQ_STOP		_IO(FIQ_IOC_MAGIC, 0xb1)
#define FIQ_RESET		_IO(FIQ_IOC_MAGIC, 0xb2)

#define FIQ_STATUS_STOPPED	(0)
#define FIQ_STATUS_RUNNING	(1 << 0)
#define FIQ_STATUS_ERR_URUN	(1 << 1)

struct fiq_buffer {
  u_int16_t data[8 * 32 * 1024];   // 8 buffers for 32 msec of samples
  // pointer to latest available full buffer
  // char of int?
  unsigned char bufp;
  unsigned char onoff;
  unsigned char status;

  //  ...
};

