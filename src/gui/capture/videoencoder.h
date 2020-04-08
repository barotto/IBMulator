/*
 * Copyright (C) 2020  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_VIDEOENCODER_H
#define IBMULATOR_VIDEOENCODER_H


enum EncoderFlags {
	ENC_FLAGS_KEYFRAME = 0x1
};

struct BitmapInfoHeader {
	uint32_t size;          // Number of bytes required by the structure.
	                        // Does not include the size of the color table or the size of the color masks.
	int32_t  width;         // Width in pixels.
	int32_t  height;        // Height in pixels.
	                        // RGB: positive = bottom-up, negative = top-down.
	                        // YUV: always top-down, regardless of the sign of height.
	                        // compressed formats: must be positive, regardless of image orientation.
	uint16_t planes;        // Must be 1.
	uint16_t bitCount;      // Bits per pixel.
	uint32_t compression;   // Compressed video and YUV formats: FOURCC code, otherwise BitmapCompression
	uint32_t sizeImage;     // Size, in bytes, of the image. This can be set to 0 for uncompressed RGB bitmaps.
	int32_t  xPelsPerMeter; // Horizontal resolution, in pixels per meter, of the target device
	int32_t  yPelsPerMeter; // Vertical resolution, in pixels per meter, of the target device
	uint32_t clrUsed;       // Number of color indices in the color table that are actually used
	uint32_t clrImportant;  // Number of color indices that are considered important. 0 = all colors are important.
	uint32_t clrMasks[4];   // Color masks (optional)
} GCC_ATTRIBUTE(packed);

enum BitmapCompression {
	BI_RGB       = 0, // Uncompressed RGB.
	BI_BITFIELDS = 3, // Uncompressed RGB with color masks for 16-bpp and 32-bpp bitmaps.
};

// Video encoders don't convert between video formats, you set them up with an image
// format and then you must feed them with uncompressed image buffers already in the expected format.
// Only palette changes are allowed.

class VideoEncoder
{
public:
	VideoEncoder() {}
	virtual ~VideoEncoder() {}
	
	virtual const char *name() = 0;
	virtual uint32_t fourcc() = 0;
	virtual const BitmapInfoHeader &format() = 0;
	virtual std::string format_string() = 0;
	
	// _format must be set with width, height
	// bitCount & clrMasks can be set with the preferred pixel format by the caller.
	// bitCount & clrMasks will be set with the expected pixel format by the encoder.
	// Other fields will be set by the encoder.
	virtual void setup_compress(BitmapInfoHeader &_format) = 0;
	
	// Returns an estimate of the byte size needed to hold a compressed frame in the worst case.
	virtual uint32_t needed_buf_size(const BitmapInfoHeader &_format) = 0;
	
	// To compress a video frame call these funcions in this order.
	virtual unsigned prepare_frame(unsigned _fmt_flags, uint8_t *_pal, uint8_t *_buf, uint32_t _bufsize) = 0;
	virtual void compress_lines(int _count, const uint8_t *_lines_data[]) = 0;
	virtual uint32_t finish_frame() = 0;
	
};

#endif