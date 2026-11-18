// Compiles stb_image (a single-header PNG decoder) once, with its own settings.
// Third-party code: it is built without the engine's strict warnings.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
