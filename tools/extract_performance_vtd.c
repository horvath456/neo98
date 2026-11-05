/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Archive extraction only. The pinned external patcher9x library is never asked to patch a file. */
#include "pew.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 4)
        return 1; /* Input VMM32.VXD, output VWIN32.VXD, temporary W3 path. */
    dos_header_t dos;
    pe_header_t header;
    FILE *input = fopen(argv[1], "rb");
    if (!input)
        return 2;
    int kind = pe_read(&dos, &header, input, 1);
    if (kind == PE_W4)
    {
        pe_w4_t *archive = pe_w4_read(&dos, &header, input);
        if (!archive)
        {
            fclose(input);
            return 3;
        }
        int result = pe_w4_to_w3(archive, argv[3]);
        pe_w4_free(archive);
        fclose(input);
        if (result != PE_OK)
            return 4;
        input = fopen(argv[3], "rb");
        if (!input)
            return 5;
        kind = pe_read(&dos, &header, input, 1);
    }
    if (kind != PE_W3)
    {
        fclose(input);
        return 6;
    }
    pe_w3_t *archive = pe_w3_read(&dos, &header, input);
    if (!archive)
    {
        fclose(input);
        return 7;
    }
    int result = pe_w3_extract(archive, "VTD", argv[2]);
    pe_w3_free(archive);
    fclose(input);
    return result == PE_OK ? 0 : 8;
}
