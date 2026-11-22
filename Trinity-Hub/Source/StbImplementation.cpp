// The one translation unit holding stb_image in Hub, for the PNG thumbnails of templates
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>