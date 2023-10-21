
#include <stdio.h>

#include "shl/defer.hpp"

#include "spirv_parser.hpp"

void print_shader_stage_flags(VkShaderStageFlags stages)
{
    array<const char *> stages_set{};
    defer { ::free(&stages_set); };

#define ADD_STAGE_IF_SET(STAGE)\
    if ((stages & STAGE) == STAGE) ::add_at_end(&stages_set, #STAGE);

    ADD_STAGE_IF_SET(VK_SHADER_STAGE_VERTEX_BIT);
    ADD_STAGE_IF_SET(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    ADD_STAGE_IF_SET(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    ADD_STAGE_IF_SET(VK_SHADER_STAGE_GEOMETRY_BIT);
    ADD_STAGE_IF_SET(VK_SHADER_STAGE_FRAGMENT_BIT);
    ADD_STAGE_IF_SET(VK_SHADER_STAGE_COMPUTE_BIT);

#undef ADD_STAGE_IF_SET

    if (stages_set.size == 0)
        printf("<no or unknown flags>");

    printf("%s", stages_set[0]);

    for (u64 i = 1; i < stages_set.size; ++i)
        printf("| %s", stages_set[i]);
}

void print_descriptor_type(VkDescriptorType type)
{
#define PRINT_DESCRIPTOR_TYPE_CASE(TYPE)\
    case TYPE: printf(#TYPE); return;

    switch (type)
    {
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_SAMPLER);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    PRINT_DESCRIPTOR_TYPE_CASE(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        default: printf("<no or unkown descriptor type>");
    }
}

void print_pipeline_info(spirv_info *info)
{
    spirv_pipeline_info pinfo{};
    init(&pinfo);
    defer { free(&pinfo); };

    get_pipeline_info(&pinfo, info);

    printf("\nPipeline info:\n");
    printf("Push constants:\n");
    
    for_array(pc, &pinfo.push_constants)
    {
        printf(R"=(  VkPushConstantRange{
    .stageFlags = )=");
        print_shader_stage_flags(pc->stageFlags);
        printf(R"=(,
    .offset = %u,
    .size   = %u
  };
)=", pc->offset, pc->size);
    }

    printf("\nDescriptor sets:\n");

    for_array(i, dset, &pinfo.descriptor_sets)
    {
        printf("  set %lu:\n", i);

        for_array(j, binding, &dset->layout_bindings)
        {
            printf("    VkDescriptorSetLayoutBinding{");
            printf(R"=(
      .binding         = %u,
      .descriptorType  = )=", binding->binding);
            print_descriptor_type(binding->descriptorType);
            printf(R"=(,
      .descriptorCount = %u,
      .stageFlags      = )=", binding->descriptorCount);
            print_shader_stage_flags(binding->stageFlags);
            printf(R"=(,
      .pImmutableSamplers = %p
    };
)=", binding->pImmutableSamplers);
        }
    }
}

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

    print_pipeline_info(&output);

    return 0;
}

