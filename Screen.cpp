// Screen routines for nPDF
// Copyright (C) 2014-2016  Legimet
//
// This file is part of nPDF.
//
// nPDF is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// nPDF is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with nPDF.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>
#include <cstdint>
#include <libndls.h>
#include "Screen.hpp"

namespace Screen {
	scr_type_t type;
	unsigned int size;
	uint8_t *screen;

	bool init() {
		type = lcd_type() == SCR_320x240_4 ? SCR_320x240_4 : SCR_320x240_565;
		if (!lcd_init(type))
			return false;
		size = (SCREEN_WIDTH*SCREEN_HEIGHT/2) * (type == SCR_320x240_4 ? 1 : 4);
		screen = new uint8_t[size];
		return true;
	}

	void deinit() {
		delete[] screen;
		lcd_init(SCR_TYPE_INVALID);
	}

	void display() {
		lcd_blit(screen, type);
	}

	void setPixel(uint8_t r, uint8_t g, uint8_t b, unsigned int x, unsigned int y) {
		// On color models, each pixel is represented in 16-bit high color
		// On classic models, each pixel is 4 bits grayscale, 0 is black and 15 is white
		if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT) {
			unsigned int pos = y * SCREEN_WIDTH + x;
			if (type == SCR_320x240_565) {
				reinterpret_cast<uint16_t*>(screen)[pos] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			} else if (pos % 2 == 0) {
				screen[pos / 2] = (screen[pos / 2] & 0x0F) | (((30 * r + 59 * g + 11 * b) / 100) & 0xF0);
			} else {
				screen[pos / 2] = (screen[pos / 2] & 0xF0) | (((30 * r + 59 * g + 11 * b) / 100) >> 4);
			}
		}
	}

	void setPixel(uint8_t c, unsigned int x, unsigned int y) {
		setPixel(c, c, c, x, y);
	}

	// The RGBA and GrayA functions display pixmaps with premultiplied alpha. The alpha component is
	// ignored since we aren't compositing

	// Fast path for the 16-bit colour models: walk the destination and source with row pointers
	// instead of recomputing y*SCREEN_WIDTH+x for every pixel, hoist the per-pixel bounds check
	// (the caller already clamps w/h to the screen) and skip the `type` branch inside the loop.
	// This is the hottest loop in the program: it runs once per visible pixel on every scroll step.
	void showImgRGB(uint8_t *img, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1,
			unsigned int w, unsigned int h, unsigned int wImg) {
		if (type == SCR_320x240_565) {
			uint16_t *srow = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)y0 * SCREEN_WIDTH + x0;
			for (unsigned int i = 0; i < h; i++) {
				const uint8_t *src = img + 3u * ((y1 + i) * wImg + x1);
				uint16_t *dst = srow;
				for (unsigned int j = 0; j < w; j++) {
					uint8_t r = src[0], g = src[1], b = src[2];
					*dst++ = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
					src += 3;
				}
				srow += SCREEN_WIDTH;
			}
			return;
		}
		unsigned int pos;
		for (unsigned int i = y1; i < y1 + h; i++) {
			for (unsigned int j = x1; j < x1 + w; j++) {
				pos = 3 * (wImg * i + j);
				setPixel(img[pos], img[pos + 1], img[pos + 2], x0 - x1 + j, y0 - y1 + i);
			}
		}
	}

	void showImgRGBA(uint8_t *img, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1,
			unsigned int w, unsigned int h, unsigned int wImg) {
		if (type == SCR_320x240_565) {
			uint16_t *srow = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)y0 * SCREEN_WIDTH + x0;
			for (unsigned int i = 0; i < h; i++) {
				const uint8_t *src = img + 4u * ((y1 + i) * wImg + x1);
				uint16_t *dst = srow;
				for (unsigned int j = 0; j < w; j++) {
					uint8_t r = src[0], g = src[1], b = src[2];
					*dst++ = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
					src += 4;
				}
				srow += SCREEN_WIDTH;
			}
			return;
		}
		unsigned int pos;
		for (unsigned int i = y1; i < y1 + h; i++) {
			for (unsigned int j = x1; j < x1 + w; j++) {
				pos = 4 * (wImg * i + j);
				setPixel(img[pos], img[pos + 1], img[pos + 2], x0 - x1 + j, y0 - y1 + i);
			}
		}
	}

	void showImgGray(uint8_t *img, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1,
			unsigned int w, unsigned int h, unsigned int wImg) {
		if (type == SCR_320x240_565) {
			uint16_t *srow = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)y0 * SCREEN_WIDTH + x0;
			for (unsigned int i = 0; i < h; i++) {
				const uint8_t *src = img + (y1 + i) * wImg + x1;
				uint16_t *dst = srow;
				for (unsigned int j = 0; j < w; j++) {
					uint8_t c = *src++;
					*dst++ = static_cast<uint16_t>(((c & 0xF8) << 8) | ((c & 0xFC) << 3) | (c >> 3));
				}
				srow += SCREEN_WIDTH;
			}
			return;
		}
		unsigned int pos;
		for (unsigned int i = y1; i < y1 + h; i++) {
			for (unsigned int j = x1; j < x1 + w; j++) {
				pos = wImg * i + j;
				setPixel(img[pos], x0 - x1 + j, y0 - y1 + i);
			}
		}
	}

	// Gray+alpha source, destination is 16-bit colour: the 4-bit gray LCD path keeps the old
	// per-pixel helper, every colour Nspire takes the pointer walk.
	void showImgGrayA(uint8_t *img, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1,
			unsigned int w, unsigned int h, unsigned int wImg) {
		if (type == SCR_320x240_565) {
			uint16_t *srow = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)y0 * SCREEN_WIDTH + x0;
			for (unsigned int i = 0; i < h; i++) {
				const uint8_t *src = img + 2u * ((y1 + i) * wImg + x1);
				uint16_t *dst = srow;
				for (unsigned int j = 0; j < w; j++) {
					uint8_t c = src[0];
					*dst++ = static_cast<uint16_t>(((c & 0xF8) << 8) | ((c & 0xFC) << 3) | (c >> 3));
					src += 2;
				}
				srow += SCREEN_WIDTH;
			}
			return;
		}
		unsigned int pos;
		for (unsigned int i = y1; i < y1 + h; i++) {
			for (unsigned int j = x1; j < x1 + w; j++) {
				pos = 2 * (wImg * i + j);
				setPixel(img[pos], x0 - x1 + j, y0 - y1 + i);
			}
		}
	}

	void fillScreen(uint8_t r, uint8_t g, uint8_t b) {
		uint16_t color;
		if (type == SCR_320x240_565) {
			color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
		} else {
			color = (30 * r + 59 * g + 11 * b) / 100;
			color = (color >> 4) * 0x1111;
		}

		std::fill(reinterpret_cast<volatile uint16_t*>(screen), reinterpret_cast<volatile uint16_t*>(screen + size), color);
	}

	void fillScreen(uint8_t c) {
		fillScreen(c, c, c);
	}

	void fillRect(uint8_t r, uint8_t g, uint8_t b, unsigned int x, unsigned int y, unsigned int w,
			unsigned int h) {
		// Row-pointer fast path: fillRect is called with screen-sized rectangles several times
		// per display() to paint the letterbox background, so the per-pixel helper is costly here.
		if (type == SCR_320x240_565) {
			uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			for (unsigned int j = y; j < y + h && j < SCREEN_HEIGHT; j++) {
				uint16_t *row = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)j * SCREEN_WIDTH;
				unsigned int xe = x + w; if (xe > SCREEN_WIDTH) xe = SCREEN_WIDTH;
				for (unsigned int i = x; i < xe; i++)
					row[i] = color;
			}
			return;
		}
		for (unsigned int i = x; i < x + w; i++) {
			for (unsigned int j = y; j < y + h; j++) {
				setPixel(r, g, b, i, j);
			}
		}
	}

	void fillRect(uint8_t c, unsigned int x, unsigned int y, unsigned int w, unsigned int h) {
		fillRect(c, c, c, x, y, w, h);
	}

	void drawVert(uint8_t r, uint8_t g, uint8_t b, unsigned int x, unsigned int y, unsigned int h) {
		if (type == SCR_320x240_565 && x < SCREEN_WIDTH) {
			uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			uint16_t *col = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)y * SCREEN_WIDTH + x;
			for (unsigned int j = 0; j < h && y + j < SCREEN_HEIGHT; j++) {
				*col = color;
				col += SCREEN_WIDTH;
			}
			return;
		}
		for (unsigned int j = y; j < y + h; j++) {
			setPixel(r, g, b, x, j);
		}
	}

	void drawHoriz(uint8_t r, uint8_t g, uint8_t b, unsigned int x, unsigned int y, unsigned int w) {
		if (type == SCR_320x240_565 && y < SCREEN_HEIGHT) {
			uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			uint16_t *row = reinterpret_cast<uint16_t*>(screen) + (uintptr_t)y * SCREEN_WIDTH + x;
			unsigned int xe = x + w; if (xe > SCREEN_WIDTH) xe = SCREEN_WIDTH;
			for (unsigned int i = x; i < xe; i++)
				*row++ = color;
			return;
		}
		for (unsigned int i = x; i < x + w; i++) {
			setPixel(r, g, b, i, y);
		}
	}
}
