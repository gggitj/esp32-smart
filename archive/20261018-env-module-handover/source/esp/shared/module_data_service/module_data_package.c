#include "module_data_service.h"

#include <stdio.h>
#include <string.h>

size_t module_data_package_read_from_directory(const char *file, uint32_t offset,
                                               uint8_t *buffer, size_t capacity,
                                               uint32_t *total, void *context)
{
    if (!file || !buffer || !capacity || !total || !context) return 0;
    if (strcmp(file, "package.json") && strcmp(file, "package.tar.zst")) return 0;
    const char *root = (const char *)context;
    char path[256];
    const int written = snprintf(path, sizeof(path), "%s/%s", root, file);
    if (written <= 0 || (size_t)written >= sizeof(path)) return 0;
    FILE *stream = fopen(path, "rb"); if (!stream) return 0;
    if (fseek(stream, 0, SEEK_END) != 0) { fclose(stream); return 0; }
    const long size = ftell(stream);
    if (size < 0 || (uint64_t)size > UINT32_MAX || offset > (uint32_t)size || fseek(stream, (long)offset, SEEK_SET) != 0) { fclose(stream); return 0; }
    *total = (uint32_t)size;
    const size_t count = fread(buffer, 1, capacity, stream);
    fclose(stream); return count;
}
