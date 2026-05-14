#ifndef RAINBOW_H
#define RAINBOW_H

static inline void get_rainbow(int index, unsigned char* r, unsigned char* g, unsigned char* b) {
	index = index % (1530);
	int section = index / 255;
	int section_offset = index % 255;
	switch (section) {
	case 0:
		*r = 255;
		*g = section_offset;
		*b = 0;
		break;
	case 1:
		*r = 255 - section_offset;
		*g = 255;
		*b = 0;
		break;
	case 2:
		*r = 0;
		*g = 255;
		*b = section_offset;
		break;
	case 3:
		*r = 0;
		*g = 255 - section_offset;
		*b = 255;
		break;
	case 4:
		*r = section_offset;
		*g = 0;
		*b = 255;
		break;
	case 5:
		*r = 255;
		*g = 0;
		*b = 255 - section_offset;
		break;
	}
}

#endif // RAINBOW_H
