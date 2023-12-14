#ifndef __JPEG_H__
#define __JPEG_H__

#include <stdint.h>
#include <turbojpeg.h>
#include <libwebsockets.h>

uint8_t *turbo_jpeg_compress(tjhandle tjh, uint8_t *input, int width, int height,
                             int bytes_per_pix, int padding, int quality,
			     unsigned long *out_size);

#endif /* __JPEG_H__ */
