
#include <stdio.h>
#include <assert.h>

#include "shl/string.hpp"
#include "shl/memory.hpp"
#include "shl/defer.hpp"
#include "spirv_parser.hpp"

#define INSTR_FMT "[%lu]"
#define INSTR_ID_FMT INSTR_FMT " %%%u ="

#define get_spirv_parse_error(ERR, FMT, ...) \
    if (ERR != nullptr) { *ERR = error{.what = format_error(FMT __VA_OPT__(,) __VA_ARGS__), .file = __FILE__, .line = __LINE__}; }

void init(spirv_id_instruction *idinstr)
{
    ::init(&idinstr->members);
}

void free(spirv_id_instruction *idinstr)
{
    ::free(&idinstr->members);
}

void init(spirv_entry_point *ep)
{
    ::init(&ep->execution_modes);
}

void free(spirv_entry_point *ep)
{
    ::free(&ep->execution_modes);
}

void init(spirv_info *info)
{
    assert(info != nullptr);

    ::fill_memory(info, 0);
}

void free(spirv_info *info)
{
    if (info == nullptr)
        return;

    ::free<true>(&info->id_instructions);
    ::free<true>(&info->entry_points);
    ::free(&info->types);
    ::free(&info->variables);
    ::free(&info->decorations);

    ::close(&info->data);
}

spirv_entry_point *get_entry_point_by_id(spirv_info *info, SpvId id)
{
    for_array(ep, &info->entry_points)
        if (ep->instruction->id == id)
            return ep;

    return nullptr;
}

void handle_spirv_op_type(u64 i, spirv_id_instruction *id_instr, const spirv_info *info)
{
    u32 bound = (u32)info->id_instructions.size;
    printf(INSTR_ID_FMT " ", i, id_instr->id);

    switch (id_instr->opcode)
    {
    case SpvOpTypeVoid:
        printf("OpTypeVoid");
        break;
    case SpvOpTypeBool:
        printf("OpTypeBool");
        break;
    case SpvOpTypeInt:
    {
        assert(id_instr->word_count == 4); 

        u32 width = id_instr->words[2];
        u32 sign  = id_instr->words[3];

        printf("OpTypeInt %u %u", width, sign);

        break;
    }
    case SpvOpTypeFloat:
    {
        assert(id_instr->word_count == 3); 

        u32 width = id_instr->words[2];

        printf("OpTypeFloat %u", width);

        break;
    }
    case SpvOpTypeVector:
    {
        assert(id_instr->word_count == 4);

        SpvId comp_id = (SpvId)id_instr->words[2];
        assert(comp_id < bound);

        u32 count = id_instr->words[3];
        assert(count >= 2);

        printf("OpTypeVector %%%u %u", comp_id, count);

        break;
    }
    case SpvOpTypeMatrix:
    {
        assert(id_instr->word_count == 4);

        SpvId column_type_id = (SpvId)id_instr->words[2];
        assert(column_type_id < bound);

        u32 column_count = id_instr->words[3];
        assert(column_count >= 2);

        printf("OpTypeMatrix %%%u %u", column_type_id, column_count);

        break;
    }
    case SpvOpTypeImage:
    {
        assert(id_instr->word_count >= 9);

        SpvId sampled_type_id = (SpvId)id_instr->words[2];
        assert(sampled_type_id < bound);

        SpvDim dim = (SpvDim)id_instr->words[3];
        u32 depth = id_instr->words[4];
        u32 arrayed = id_instr->words[5];
        u32 multisampled = id_instr->words[6];
        u32 sampled = id_instr->words[7];
        SpvImageFormat format = (SpvImageFormat)id_instr->words[8];

        printf("OpTypeImage %%%u %d %u %u %u %u %d", sampled_type_id, dim, depth, arrayed, multisampled, sampled, format);

        if (id_instr->word_count >= 10)
        {
            SpvAccessQualifier access = (SpvAccessQualifier)(id_instr->words[9]);
            printf(" %d", access);
        }

        break;
    }
    case SpvOpTypeSampler:
        printf("OpTypeSampler");
        break;
    case SpvOpTypeSampledImage:
    {
        assert(id_instr->word_count == 3);

        SpvId img_id = (SpvId)id_instr->words[2];
        assert(img_id < bound);

        printf("OpTypeSampledImage %%%u", img_id);
        break;
    }
    case SpvOpTypeArray:
    {
        assert(id_instr->word_count == 4);

        SpvId comp_id = (SpvId)id_instr->words[2];
        assert(comp_id < bound);

        SpvId length_id = (SpvId)id_instr->words[3];
        assert(length_id < bound);

        printf("OpTypeArray %%%u %%%u", comp_id, length_id);
        break;
    }
    case SpvOpTypeRuntimeArray:
    {
        assert(id_instr->word_count == 3);

        SpvId comp_id = (SpvId)id_instr->words[2];
        assert(comp_id < bound);

        printf("OpTypeRuntimeArray %%%u", comp_id);
        break;
    }
    case SpvOpTypeStruct:
    {
        assert(id_instr->word_count >= 2);

        printf("OpTypeStruct");

        SpvId *member_ids = (SpvId*)(id_instr->words + 2);
        u32 member_count = id_instr->word_count - 2;

        ::reserve(&id_instr->members, member_count);
        id_instr->members.size = member_count;

        for (u32 mem_i = 0; mem_i < member_count; ++mem_i)
        {
            SpvId mem_id = member_ids[mem_i];
            id_instr->members[mem_i].type_id = mem_id;
            printf(" %%%u", mem_id);
        }

        break;
    }
    case SpvOpTypeOpaque:
    {
        assert(id_instr->word_count >= 2);

        const char *opaque_type_name = (const char *)(id_instr->words + 2);

        printf("OpTypeOpaque %s", opaque_type_name);

        break;
    }
    case SpvOpTypePointer:
    {
        assert(id_instr->word_count == 4);

        SpvStorageClass storage = (SpvStorageClass)(id_instr->words[2]);
        SpvId type_id = (SpvId)(id_instr->words[3]);

        printf("OpTypePointer %d %%%u", storage, type_id);

        break;
    }
    case SpvOpTypeFunction:
    {
        assert(id_instr->word_count >= 3);

        SpvId return_type_id = (SpvId)id_instr->words[2];

        printf("OpTypeFunction %%%u", return_type_id);

        SpvId *param_ids = (SpvId*)(id_instr->words + 3);
        u32 param_count = id_instr->word_count - 3;

        for (u32 param_i = 0; param_i < param_count; ++param_i)
        {
            SpvId param_id = param_ids[param_i];
            printf(" %%%u", param_id);
        }

        break;
    }
    case SpvOpTypeEvent:
        printf("OpTypeEvent");
        break;
    case SpvOpTypeDeviceEvent:
        printf("OpTypeDeviceEvent");
        break;
    case SpvOpTypeReserveId:
        printf("OpTypeReserveId");
        break;
    case SpvOpTypeQueue:
        printf("OpTypeQueue");
        break;
    case SpvOpTypePipe:
    {
        assert(id_instr->word_count == 3);

        SpvAccessQualifier access = (SpvAccessQualifier)(id_instr->words[2]);

        printf("OpTypePipe %d", access);
        break;
    }
    case SpvOpTypePipeStorage:
        printf("OpTypePipeStorage");
        break;
    case SpvOpTypeNamedBarrier:
        printf("OpTypeNamedBarrier");
        break;
    }

    printf("\n");
}

void handle_spirv_op_variable(u64 i, spirv_id_instruction *id_instr, const spirv_info *info)
{
    u32 bound = (u32)info->id_instructions.size;
    SpvId result_type_id = (SpvId)id_instr->words[1];
    assert(result_type_id < bound);

    spirv_id_instruction *result_instr = info->id_instructions.data + result_type_id;

    printf(INSTR_ID_FMT " ", i, id_instr->id);

    switch (id_instr->opcode)
    {
    case SpvOpVariable:
    {
        assert(id_instr->word_count >= 4);

        SpvStorageClass storage = (SpvStorageClass)(id_instr->words[3]);

        printf("OpVariable %%%u %d", result_type_id, storage);

        if (id_instr->word_count > 4)
        {
            SpvId init_type = (SpvId)id_instr->words[4];
            printf(" %%%u", init_type);
        }

        // maybe handle larger type constants
        break;
    }
    case SpvOpConstant:
    case SpvOpSpecConstant:
    {
        assert(id_instr->word_count >= 4);

        u32 *val = id_instr->words + 3;

        if (id_instr->opcode == SpvOpConstant)
            printf("OpConstant %%%u", result_type_id);
        else
            printf("OpSpecConstant %%%u", result_type_id);

        switch (result_instr->opcode)
        {
        case SpvOpTypeInt:   printf(" %u", *val); break;
        case SpvOpTypeFloat: printf(" %f", *(float*)(val)); break;
        default: break;
        }

        // maybe handle larger type constants
        break;
    }
    case SpvOpConstantNull:
        printf("OpContantNull %%%u", result_type_id);
        break;
    case SpvOpConstantTrue:
        printf("OpContantTrue %%%u", result_type_id);
        break;
    case SpvOpConstantFalse:
        printf("OpContantFalse %%%u", result_type_id);
        break;
    case SpvOpConstantComposite:
    case SpvOpSpecConstantComposite:
    {
        assert(id_instr->word_count >= 3);

        if (id_instr->opcode == SpvOpConstantComposite)
            printf("OpConstantComposite %%%u", result_type_id);
        else
            printf("OpSpecConstantComposite %%%u", result_type_id);

        SpvId *constituents_ids = (SpvId*)(id_instr->words + 3);
        u32 constituents_count = id_instr->word_count - 3;

        for (u32 constituents_i = 0; constituents_i < constituents_count; ++constituents_i)
        {
            SpvId constituents_id = constituents_ids[constituents_i];
            printf(" %%%u", constituents_id);
        }

        break;
    }
    case SpvOpConstantSampler:
    {
        assert(id_instr->word_count == 6);

        SpvSamplerAddressingMode addr_mode = (SpvSamplerAddressingMode)id_instr->words[3];
        u32 normalized = id_instr->words[4];
        SpvSamplerFilterMode filter = (SpvSamplerFilterMode)id_instr->words[5];

        printf("OpConstantSampler %%%u %d %u %d", result_type_id, addr_mode, normalized, filter);
        break;
    }
    case SpvOpSpecConstantOp:
    {
        assert(id_instr->word_count >= 4);

        u32 opcode = id_instr->words[3];

        u32 *operands = id_instr->words + 4;
        u32 operand_count = id_instr->word_count - 4;

        printf("OpSpecConstantOp %%%u %u", result_type_id, opcode);

        for (u32 op_i = 0; op_i < operand_count; ++op_i)
            printf(" %u", operands[op_i]);

        break;
    }
    case SpvOpSpecConstantTrue:
        printf("OpSpecContantTrue %%%u", result_type_id);
        break;
    case SpvOpSpecConstantFalse:
        printf("OpSpecContantFalse %%%u", result_type_id);
        break;
    default: 
        break;
    }

    printf("\n");
}

void handle_spirv_decoration(u32 *words, u32 word_count, SpvDecoration decoration, const spirv_info *info)
{
    switch (decoration)
    {
    case SpvDecorationRelaxedPrecision: printf(" RelaxedPrecision"); break;
    case SpvDecorationSpecId:
    {
        assert(word_count == 1);
        u32 spec_id = words[0];
        printf(" SpecId %u", spec_id);
        break;
    }
    case SpvDecorationBlock: printf(" Block"); break;
    case SpvDecorationBufferBlock: printf(" BufferBlock"); break;
    case SpvDecorationRowMajor: printf(" RowMajor"); break;
    case SpvDecorationColMajor: printf(" ColMajor"); break;
    case SpvDecorationArrayStride: 
    {
        assert(word_count == 1);
        u32 stride = words[0];
        printf(" ArrayStride %u", stride);
        break;
    }
    case SpvDecorationMatrixStride:
    {
        assert(word_count == 1);
        u32 stride = words[0];
        printf(" MatrixStride %u", stride);
        break;
    }
    case SpvDecorationGLSLShared: printf(" GLSLShared"); break;
    case SpvDecorationGLSLPacked: printf(" GLSLPacked"); break;
    case SpvDecorationCPacked: printf(" CPacked"); break;
    case SpvDecorationBuiltIn:
    {
        assert(word_count == 1);
        SpvBuiltIn builtin = (SpvBuiltIn)words[0];
        printf(" BuiltIn %d", builtin);
        break;
    }
    case SpvDecorationNoPerspective: printf(" NoPerspective"); break;
    case SpvDecorationFlat: printf(" Flat"); break;
    case SpvDecorationPatch: printf(" Patch"); break;
    case SpvDecorationCentroid: printf(" Centroid"); break;
    case SpvDecorationSample: printf(" Sample"); break;
    case SpvDecorationInvariant: printf(" Invariant"); break;
    case SpvDecorationRestrict: printf(" Restrict"); break;
    case SpvDecorationAliased: printf(" Aliased"); break;
    case SpvDecorationVolatile: printf(" Volatile"); break;
    case SpvDecorationConstant: printf(" Constant"); break;
    case SpvDecorationCoherent: printf(" Coherent"); break;
    case SpvDecorationNonWritable: printf(" NonWritable"); break;
    case SpvDecorationNonReadable: printf(" NonReadable"); break;
    case SpvDecorationUniform: printf(" Uniform"); break;
    case SpvDecorationSaturatedConversion: printf(" SaturatedConversion"); break;
    case SpvDecorationStream:
    {
        assert(word_count == 1);
        u32 stream = words[0];
        printf(" Stream %u", stream);
        break;
    }
    case SpvDecorationLocation: 
    {
        assert(word_count == 1);
        u32 location = words[0];
        printf(" Location %u", location);
        break;
    }
    case SpvDecorationComponent:
    {
        assert(word_count == 1);
        u32 component = words[0];
        printf(" Component %u", component);
        break;
    }
    case SpvDecorationIndex:
    {
        assert(word_count == 1);
        u32 index = words[0];
        printf(" Index %u", index);
        break;
    }
    case SpvDecorationBinding:
    {
        assert(word_count == 1);
        u32 binding = words[0];
        printf(" Binding %u", binding);
        break;
    }
    case SpvDecorationDescriptorSet:
    {
        assert(word_count == 1);
        u32 set = words[0];
        printf(" DescriptorSet %u", set);
        break;
    }
    case SpvDecorationOffset:
    {
        assert(word_count == 1);
        u32 offset = words[0];
        printf(" Offset %u", offset);
        break;
    }
    case SpvDecorationXfbBuffer: printf(" XfbBuffer"); break;
    {
        assert(word_count == 1);
        u32 buffer = words[0];
        printf(" XfbBuffer %u", buffer);
        break;
    }
    case SpvDecorationXfbStride: printf(" XfbStride"); break;
    {
        assert(word_count == 1);
        u32 stride = words[0];
        printf(" XfbStride %u", stride);
        break;
    }
    case SpvDecorationFuncParamAttr:
    {
        assert(word_count == 1);
        SpvFunctionParameterAttribute attr = (SpvFunctionParameterAttribute)words[0];
        printf(" FuncParamAttr %d", attr);
        break;
    }
    case SpvDecorationFPRoundingMode:
    {
        assert(word_count == 1);
        SpvFPRoundingMode mode = (SpvFPRoundingMode)words[0];
        printf(" FPRoundingMode %d", mode);
        break;
    }
    case SpvDecorationFPFastMathMode:
    {
        assert(word_count == 1);
        SpvFPFastMathModeMask mode = (SpvFPFastMathModeMask)words[0];
        printf(" FPFastMathMode %d", mode);
        break;
    }
    case SpvDecorationLinkageAttributes:
    {
        assert(word_count >= 2);
        // WHY IS THE STRING THE FIRST ARGUMENT AGAIN
        // WHAT IS WRONG WITH YOU
        // I'm just going to ignore the linkage argument type.
        // Screw you khronos.
        const char *name = (const char *)words;

        printf(" LinkageAttributes %s ?", name);
        break;
    }
    case SpvDecorationInputAttachmentIndex: printf(" "); break;
    {
        assert(word_count == 1);
        u32 index = words[0];
        printf(" InputAttachmentIndex %u", index);
        break;
    }
    case SpvDecorationAlignment:
    {
        assert(word_count == 1);
        u32 align = words[0];
        printf(" Alignment %u", align);
        break;
    }
    case SpvDecorationMaxByteOffset:
    {
        assert(word_count == 1);
        u32 offset = words[0];
        printf(" MaxByteOffset %u", offset);
        break;
    }
    case SpvDecorationAlignmentId:
    {
        assert(word_count == 1);
        SpvId id = (SpvId)words[0];
        printf(" AlignmentId %d", id);
        break;
    }
    case SpvDecorationMaxByteOffsetId:
    {
        assert(word_count == 1);
        SpvId id = (SpvId)words[0];
        printf(" MaxByteOffsetId %d", id);
        break;
    }
    default: break;
    }
}

bool parse_spirv_from_memory(memory_stream *input, spirv_info *output, error *err)
{
    assert(input != nullptr);
    assert(output != nullptr);

    if (input->size < 20)
    {
        get_spirv_parse_error(err, "input file too small");
        return false;
    }

    output->data = *input;

    u32 magic;

    read(input, &magic);

    if (magic != SpvMagicNumber)
    {
        get_spirv_parse_error(err, "invalid magic number, expected %u but got %u\n", magic);
        return false;
    }

    // https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_execution_environment_and_client_api
    u32 version;
    u32 gen_magic;
    u32 bound;

    read(input, &version);
    read(input, &gen_magic);
    read(input, &bound);

    printf("version:         %u.%u (%08x)\n", (version >> 16) & 0xff, (version >> 8) & 0xff, version);
    printf("generator magic: %08x\n", gen_magic);
    printf("bound:           %u\n", bound);

    ::resize(&output->id_instructions, bound);
    ::fill_memory(output->id_instructions.data, 0, output->id_instructions.size * sizeof(spirv_id_instruction));

    for_array(i, _idinstr, &output->id_instructions)
    {
        _idinstr->id = (SpvId)i;
        init(_idinstr);
    }

    array<spirv_instruction> instructions{};
    defer { ::free(&instructions); };

    input->position += sizeof(u32);

    while (!::is_at_end(input))
    {
        spirv_instruction instr{};

        instr.words = (u32*)::current(input);
        read(input, &instr.opcode);
        read(input, &instr.word_count);

        assert(instr.word_count >= 1);
        input->position += sizeof(u32) * (instr.word_count - 1);
        ::add_at_end(&instructions, instr);
    }

    if (instructions.size <= 0)
    {
        get_spirv_parse_error(err, "no instructions");
        return false;
    }

    u64 instruction_count = instructions.size;

    // https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_logical_layout_of_a_module
    // The spec really is nonsense, they could've trivially added delimiters between
    // sections, put section information at the start or even just made the opcodes
    // sequential so you can simply check if an opcode is within the range of a
    // section, but no.

    spirv_instruction *instr;
    u64 i = 0;
    bool breakout = false;

    printf("\nMode Setting\n");

    // 1. OpCapability
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpCapability)
            break;

        assert(instr->word_count == 2);

        SpvCapability cap = (SpvCapability)instr->words[1];
        printf(INSTR_FMT " OpCapability %d\n", i, cap);
    }

    // 2. OpExtension
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpExtension)
            break;

        assert(instr->word_count >= 2);

        const char *name = (const char *)(instr->words + 1);
        printf(INSTR_FMT " OpExtension %s\n", i, name);
    }

    // 3. OpExtInstImport
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpExtInstImport)
            break;

        assert(instr->word_count >= 3);

        u32 id = instr->words[1];
        const char *name = (const char *)(instr->words + 2);

        ::copy_memory(instr, output->id_instructions.data + id, sizeof(spirv_instruction));

        printf(INSTR_ID_FMT " OpExtInstImport %s\n", i, id, name);
    }

    // 4. OpMemoryModel (required)
    if (i >= instruction_count || instr->opcode != SpvOpMemoryModel)
    {
        get_spirv_parse_error(err, "required OpMemoryModel instruction not found");
        return false;
    }

    assert(instr->word_count == 3);

    output->addressing_model = (SpvAddressingModel)instr->words[1];
    output->memory_model = (SpvMemoryModel)instr->words[2];

    printf(INSTR_FMT " OpMemoryModel %d %d\n", i, output->addressing_model, output->memory_model);
    i++;

    // 5. OpEntryPoint
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpEntryPoint)
            break;

        assert(instr->word_count >= 4);

        SpvId id = (SpvId)instr->words[2];
        assert(id < bound);

        spirv_id_instruction *id_instr = output->id_instructions.data + id;

        ::copy_memory(instr, id_instr, sizeof(spirv_instruction));

        spirv_entry_point *ep = ::add_at_end(&output->entry_points);
        ::init(ep);

        ep->instruction = id_instr;
        ep->execution_model = (SpvExecutionModel)instr->words[1];
        ep->name = (const char *)(instr->words + 3);
        // why is name not last??????????
        u64 name_wordlen = ((string_length(ep->name) + 3) / 4);

        assert(name_wordlen + 4 < instr->word_count);

        ep->ref_count = (instr->word_count - 4) - name_wordlen;
        ep->refs = instr->words + 5;

        printf(INSTR_FMT " OpEntryPoint %d %%%u %s", i, ep->execution_model, id, ep->name);

        for (u32 ref = 0; ref < ep->ref_count; ++ref)
            printf(" %%%u", ep->refs[ref]);
        printf("\n");
    }

    // 6. OpExecutionMode / OpExecutionModeId
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpExecutionMode
         && instr->opcode != SpvOpExecutionModeId)
            break;

        assert(instr->word_count >= 3);

        SpvId id = (SpvId)instr->words[1];
        spirv_entry_point *ep = get_entry_point_by_id(output, id);

        if (ep == nullptr)
        {
            get_spirv_parse_error(err, INSTR_FMT " invalid OpExecutionMode entry point %u", i, id);
            return false;
        }

        spirv_entry_point_execution_mode *exec = ::add_at_end(&ep->execution_modes);

        exec->execution_mode = (SpvExecutionMode)instr->words[2];
        exec->words = instr->words + 3;
        exec->word_count = instr->word_count - 3;

        printf(INSTR_FMT " OpExecutionMode %u %d\n", i, id, exec->execution_mode);
    }

    printf("\nDebug Information\n");

    // 7. Debug instructions
    // 7.a String & Sources
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpString
         && instr->opcode != SpvOpSource
         && instr->opcode != SpvOpSourceExtension
         && instr->opcode != SpvOpSourceContinued
         )
            break;

        switch (instr->opcode)
        {
        case SpvOpString:
        {
            assert(instr->word_count >= 3);
            SpvId id = (SpvId)instr->words[1];

            assert(id < bound);

            const char *value = (const char *)(instr->words + 2);

            ::copy_memory(instr, output->id_instructions.data + id, sizeof(spirv_instruction));

            printf(INSTR_ID_FMT " OpString \"%s\"\n", i, id, value);
            break;
        };

        case SpvOpSource:
        {
            assert(instr->word_count >= 3);
            SpvSourceLanguage lang = (SpvSourceLanguage)instr->words[1];
            u32 sourcever = instr->words[2];

            printf(INSTR_FMT " OpSource %d %u", i, lang, sourcever);

            if (instr->word_count >= 4)
            {
                SpvId fileid = (SpvId)instr->words[3];
                assert(fileid < bound);

                printf(" %%%u", fileid);
            }

            if (instr->word_count >= 5)
            {
                const char *source = (const char *)(instr->words + 4);

                printf(" %s", source);
            }

            printf("\n");

            break;
        };

        case SpvOpSourceExtension:
        {
            assert(instr->word_count >= 2);

            const char *ext = (const char *)(instr->words + 1);

            printf(INSTR_FMT " OpSourceExtension %s\n", i, ext);
            break;
        };

        case SpvOpSourceContinued:
        {
            assert(instr->word_count >= 2);

            const char *cont = (const char *)(instr->words + 1);

            printf(INSTR_FMT " OpSourceContinued %s\n", i, cont);
            break;
        };

        default:
        break;
        }
    }

    // 7.b OpName and OpMemberName
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpName
         && instr->opcode != SpvOpMemberName
         )
            break;

        switch (instr->opcode)
        {
        case SpvOpName:
        {
            assert(instr->word_count >= 3);
            SpvId id = (SpvId)instr->words[1];
            assert(id < bound);

            spirv_id_instruction *idinstr = output->id_instructions.data + id;

            const char *name = (const char *)(instr->words + 2);

            idinstr->name = name;

            printf(INSTR_FMT " OpName %%%u \"%s\"\n", i, id, name);

            break;
        }

        case SpvOpMemberName:
        {
            assert(instr->word_count >= 4);
            SpvId id = (SpvId)instr->words[1];
            assert(id < bound);

            spirv_id_instruction *idinstr = output->id_instructions.data + id;

            u32 member = instr->words[2];
            const char *member_name = (const char *)(instr->words + 3);

            if (member >= idinstr->members.reserved_size)
                ::reserve(&idinstr->members, member + 4);

            if (member >= idinstr->members.size)
                idinstr->members.size = member + 1;

            idinstr->members[member].name = member_name;

            printf(INSTR_FMT " OpMemberName %%%u %u \"%s\"\n", i, id, member, member_name);

            break;
        }

        default:
            break;
        }
    }

    // 7.c OpModuleProcessed
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        if (instr->opcode != SpvOpModuleProcessed)
            break;

        assert(instr->word_count >= 2);

        const char *process = (const char *)(instr->words + 1);
        printf(INSTR_FMT " OpModuleProcessed %s\n", i, process);
        break;
    }

    // 8. Decorations
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        switch (instr->opcode)
        {
        case SpvOpDecorate:
        {
            assert(instr->word_count >= 3);
            ::add_at_end(&output->decorations, *instr);
            
            SpvId target_id = (SpvId)instr->words[1];
            assert(target_id < bound);

            printf(INSTR_FMT " OpDecorate %%%u", i, target_id);

            SpvDecoration decoration = (SpvDecoration)instr->words[2];

            handle_spirv_decoration(instr->words + 3, instr->word_count - 3, decoration, output);

            printf("\n");
            continue;
        }
        case SpvOpMemberDecorate:
        {
            assert(instr->word_count >= 4);
            ::add_at_end(&output->decorations, instr);
            
            SpvId target_type_id = (SpvId)instr->words[1];
            assert(target_type_id < bound);

            u32 member = instr->words[2];

            printf(INSTR_FMT " OpMemberDecorate %%%u %u", i, target_type_id, member);

            SpvDecoration decoration = (SpvDecoration)instr->words[3];

            handle_spirv_decoration(instr->words + 4, instr->word_count - 4, decoration, output);

            printf("\n");
            continue;
        }
        case SpvOpDecorateId:
        {
            assert(instr->word_count >= 3);
            ::add_at_end(&output->decorations, instr);
            
            SpvId target_id = (SpvId)instr->words[1];
            assert(target_id < bound);

            printf(INSTR_FMT " OpDecorateId %%%u", i, target_id);

            SpvDecoration decoration = (SpvDecoration)instr->words[2];

            handle_spirv_decoration(instr->words + 3, instr->word_count - 3, decoration, output);

            printf("\n");
            continue;
        }
        case SpvOpDecorationGroup:
        case SpvOpGroupDecorate:
        case SpvOpGroupMemberDecorate:
            continue;
        }

        break;
    }

    // 9. Type declarations
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        switch (instr->opcode)
        {
        case SpvOpTypeVoid:
        case SpvOpTypeBool:
        case SpvOpTypeInt:
        case SpvOpTypeFloat:
        case SpvOpTypeVector:
        case SpvOpTypeMatrix:
        case SpvOpTypeImage:
        case SpvOpTypeSampler:
        case SpvOpTypeSampledImage:
        case SpvOpTypeArray:
        case SpvOpTypeRuntimeArray:
        case SpvOpTypeStruct:
        case SpvOpTypeOpaque:
        case SpvOpTypePointer:
        case SpvOpTypeFunction:
        case SpvOpTypeEvent:
        case SpvOpTypeDeviceEvent:
        case SpvOpTypeReserveId:
        case SpvOpTypeQueue:
        case SpvOpTypePipe:
        case SpvOpTypePipeStorage:
        case SpvOpTypeNamedBarrier:
        {
            assert(instr->word_count >= 2);
            SpvId id = (SpvId)instr->words[1];
            assert(id < bound);

            spirv_id_instruction *id_instr = output->id_instructions.data + id;

            ::copy_memory(instr, id_instr, sizeof(spirv_instruction));
            ::add_at_end(&output->types, spirv_type{ .instruction = id_instr });
            handle_spirv_op_type(i, id_instr, output);
            break;
        }

        case SpvOpVariable:
        case SpvOpConstant:
        case SpvOpConstantNull:
        case SpvOpConstantTrue:
        case SpvOpConstantFalse:
        case SpvOpConstantComposite:
        case SpvOpConstantSampler:
        case SpvOpSpecConstant:
        case SpvOpSpecConstantOp:
        case SpvOpSpecConstantTrue:
        case SpvOpSpecConstantFalse:
        case SpvOpSpecConstantComposite:
        {
            assert(instr->word_count >= 3);
            // words[1] is result type
            SpvId id = (SpvId)instr->words[2];
            assert(id < bound);

            spirv_id_instruction *id_instr = output->id_instructions.data + id;

            ::copy_memory(instr, id_instr, sizeof(spirv_instruction));
            ::add_at_end(&output->variables, spirv_variable{ .instruction = id_instr });
            handle_spirv_op_variable(i, id_instr, output);
            break;
        }

        case SpvOpTypeForwardPointer:
        case SpvOpLine:
        case SpvOpNoLine:
            continue;

        default:
            breakout = true;
            break;
        }

        if (breakout)
        {
            breakout = false;
            break;
        }
    }

    // 10. & 11. Functions
    for (; i < instruction_count; ++i)
    {
        instr = instructions.data + i;

        assert(instr->opcode == SpvOpFunction);
        assert(instr->word_count == 5);

        SpvId result_type_id = (SpvId)instr->words[1];
        assert(result_type_id < bound);

        SpvId result_id = (SpvId)instr->words[2];
        assert(result_id < bound);

        SpvFunctionControlMask control_mask = (SpvFunctionControlMask)instr->words[3];

        SpvId function_type_id = (SpvId)instr->words[4];
        assert(function_type_id < bound);

        printf(INSTR_ID_FMT " OpFunction %%%u %d %%%u\n", i, result_id, result_type_id, control_mask, function_type_id);

        ++i;
        for (; i < instruction_count; ++i)
        {
            spirv_instruction *finstr = instructions.data + i;

            switch (finstr->opcode)
            {
            case SpvOpFunctionParameter:
            {
                assert(instr->opcode == SpvOpFunction);
                assert(instr->word_count == 3);

                SpvId result_type_id = (SpvId)instr->words[1];
                assert(result_type_id < bound);

                SpvId result_id = (SpvId)instr->words[2];
                assert(result_id < bound);

                printf(INSTR_ID_FMT " OpFunctionParameter %%%u\n", i, result_id, result_type_id);

                break;
            }
            case SpvOpFunctionEnd:
            {
                assert(finstr->word_count == 1);

                printf(INSTR_FMT " OpFunctionEnd\n", i);

                breakout = true;
                break;
            }

            default:
                break;
            }

            if (breakout)
            {
                breakout = false;
                break;
            }
        }
    }

    return true;
}

bool parse_spirv_from_file(const char *file, spirv_info *output, error *err)
{
    assert(file != nullptr);
    assert(output != nullptr);

    memory_stream *mem = &output->data;
    ::init(mem);

    if (!::read_entire_file(file, mem, err))
        return false;

    if (!parse_spirv_from_memory(mem, output, err))
        return false;

    return true;
}

