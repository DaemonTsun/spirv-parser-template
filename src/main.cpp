
#include <stdio.h>

#include "shl/defer.hpp"

#include "spirv_parser.hpp"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("error: no input file\n");
        return 1;
    }

    spirv_info output{};
    defer { free(&output); };
    init(&output);

    error err{};

    if (!parse_spirv_from_file(argv[1], &output, &err))
    {
        printf("error: %s", err.what);
        return 2;
    }

    return 0;
}

