
#include "jpeg.h"

static uint8_t *yuyv_align422(uint8_t *input, int width, int height, int bytes_per_pix)
{
	int h, w, i, out_size, wxh;
	int yoff, uoff, voff, ioff;
	uint8_t *output;

	if (bytes_per_pix != 2 && bytes_per_pix != 1) {
		lwsl_err("%s: only 1 or 2 bytes per pixel supported\n", __func__);
		return NULL;
	}

	wxh = (width * height);
	out_size = wxh + ((2 * wxh) / bytes_per_pix);

	yoff = 0;
	uoff = wxh;
	voff = wxh + (wxh / bytes_per_pix);

	output = malloc(out_size);
	if (!output)
		return NULL;

	w = (width * bytes_per_pix);
	for (ioff = 0, h = 0; h < height; h++, ioff += w) {
		/* align to 4:2:2
		   YYYYYYYYY
		   YYYYYYYYY
		   .....
		   UUUUUUU
		   VVVVVVV */
		uint8_t *p_input = &input[ioff];
		for (i = 0; i < w; i += 4) {
			output[yoff++] = *(p_input++);
			output[uoff++] = *(p_input++);
			output[yoff++] = *(p_input++);
			output[voff++] = *(p_input++);
		}
	}

	return output;
}

uint8_t *turbo_jpeg_compress(tjhandle tjh, uint8_t *input, int width, int height,
			     int bytes_per_pix, int padding, int quality, unsigned long *out_size)
{
	uint8_t *yuv422_buf, *jpeg_buf = NULL;

	yuv422_buf = yuyv_align422(input, width, height, bytes_per_pix);
	if (!yuv422_buf) {
		lwsl_err("%s: failed to align to YUV 422\n", __func__);
		return NULL;
	}

	if (tjCompressFromYUV(tjh, yuv422_buf, width, padding, height,
	    TJSAMP_422, &jpeg_buf, out_size, quality, 0)) {
		free(yuv422_buf);
		lwsl_err("%s: error: %s\n", __func__, tjGetErrorStr());
		return NULL;
	}

	free(yuv422_buf);

	return jpeg_buf;
}

