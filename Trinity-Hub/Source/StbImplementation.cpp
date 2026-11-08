// The one translation unit holding stb_image's implementation in Hub, for the PNG thumbnails of a version's templates, which New Project shows. Hub reads them into memory itself, so stb needs no file access of its own
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>