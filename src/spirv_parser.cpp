
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

#define UNCALCULATED max_value(u32)

bool next_instruction(memory_stream *stream, spirv_instruction *out)
{
    u64 diff = (u8*)out->words - (u8*)stream->data;
    u64 len = out->word_count * sizeof(u32);
    u64 offset = diff + len;

    if (offset > stream->size)
        return false;

    assert(stream->size - offset >= 4);

    out->words = (u32*)(stream->data + offset);

    read_at(stream, &out->opcode, offset);
    read_at(stream, &out->word_count, offset + sizeof(u16));

    assert(out->word_count >= 1);
    return true;
}

void init(spirv_id_instruction *instr)
{
    ::init(&instr->decoration_indices);
    instr->extra = max_value(u32);
}

void free(spirv_id_instruction *instr)
{
    ::free(&instr->decoration_indices);
}

void init(spirv_function *func)
{
    ::init(&func->called_function_indices);
    ::init(&func->referenced_variables);
}

void free(spirv_function *func)
{
    ::free(&func->called_function_indices);
    ::free(&func->referenced_variables);
}

void init(spirv_entry_point *ep)
{
    ::fill_memory(ep, 0);
    ep->function_index = max_value(u32);
    ::init(&ep->execution_modes);
}

void free(spirv_entry_point *ep)
{
    ::free(&ep->execution_modes);
}

void init(spirv_type *type)
{
    ::init(&type->members);
}

void free(spirv_type *type)
{
    ::free(&type->members);
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
    ::free<true>(&info->types);
    ::free<true>(&info->functions);
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

spirv_type *_get_type_by_id(spirv_info *info, SpvId id)
{
    return info->types.data + info->id_instructions[id].extra;
}

spirv_variable *_get_variable_by_id(spirv_info *info, SpvId id)
{
    return info->variables.data + info->id_instructions[id].extra;
}

void _add_referenced_variable_by_id(SpvId id, spirv_function *func, spirv_info *info)
{
    u32 index = info->id_instructions[id].extra;

    if (index == max_value(u32))
        return;

    if (index >= info->variables.size)
        return;

    ::insert_element(&func->referenced_variables, info->variables.data + index);
}

void collect_function_used_variables(spirv_function *func, spirv_info *info)
{
    spirv_instruction instr;
    ::copy_memory(func->instruction, &instr, sizeof(spirv_instruction));

    while (next_instruction(&info->data, &instr))
    {
        if (instr.opcode == SpvOpFunctionEnd)
            break;

        switch (instr.opcode)
        {
        case SpvOpAccessChain:
        {
            // we only really care about base_id, the rest is fields inside it
            SpvId base_id = (SpvId)instr.words[3];
            _add_referenced_variable_by_id(base_id, func, info);
            break;
        }
        case SpvOpLoad:
        {
            SpvId base_id = (SpvId)instr.words[3];
            _add_referenced_variable_by_id(base_id, func, info);
            break;
        }

        }
    }

    for_array(var, &func->referenced_variables)
        printf("function %s (%%%u) references variable %%%u\n", func->instruction->name, func->instruction->id, (*var)->instruction->id);
}

void collect_function_information(spirv_info *info)
{
    // we want to know all variables referenced in entry points
    // so that we can generate DescriptorSet layouts
    
    for_array(func, &info->functions)
        collect_function_used_variables(func, info);
}

const char *storage_class_name(SpvStorageClass storage)
{
    switch (storage)
    {
    case SpvStorageClassUniformConstant:    return "uniform_constant";
    case SpvStorageClassInput:              return "input";
    case SpvStorageClassUniform:            return "uniform";
    case SpvStorageClassOutput:             return "output";
    case SpvStorageClassWorkgroup:          return "workgroup";
    case SpvStorageClassCrossWorkgroup:     return "cross_workgroup";
    case SpvStorageClassPrivate:            return "private";
    case SpvStorageClassFunction:           return "function";
    case SpvStorageClassGeneric:            return "generic";
    case SpvStorageClassPushConstant:       return "push_constant";
    case SpvStorageClassAtomicCounter:      return "atomic_counter";
    case SpvStorageClassImage:              return "image";
    case SpvStorageClassStorageBuffer:      return "storage_buffer";
    default: return "";
    }

    return "";
}

u64 calculate_type_size(spirv_type *t, spirv_info *info)
{
    if (t->size != UNCALCULATED)
        return t->size;

    switch (t->instruction->opcode)
    {
    case SpvOpTypeVoid:   return 0;
    case SpvOpTypeBool:   return 0;
    case SpvOpTypeInt:    return t->instruction->words[2] / 8;
    case SpvOpTypeFloat:  return t->instruction->words[2] / 8;
    case SpvOpTypeVector:
    {
        SpvId comp_id = (SpvId)t->instruction->words[2];
        spirv_type *comp_type = _get_type_by_id(info, comp_id);
        u32 comp_count = t->instruction->words[3];
        return calculate_type_size(comp_type, info) * comp_count;
    }
    case SpvOpTypeMatrix:
    {
        SpvId comp_id = (SpvId)t->instruction->words[2];
        spirv_type *comp_type = _get_type_by_id(info, comp_id);
        u32 comp_count = t->instruction->words[3];
        return calculate_type_size(comp_type, info) * comp_count;
    }
    case SpvOpTypeImage:        return 0;
    case SpvOpTypeSampler:      return 0;
    case SpvOpTypeSampledImage: return 0;
    case SpvOpTypeArray:
    {
        SpvId elem_type_id = (SpvId)t->instruction->words[2];
        spirv_type *elem_type = _get_type_by_id(info, elem_type_id);

        SpvId length_var_id = (SpvId)t->instruction->words[3];
        spirv_variable *length_var = _get_variable_by_id(info, length_var_id);

        // maybe handle larger types
        u32 length = length_var->instruction->words[3];
        
        return calculate_type_size(elem_type, info) * length;
    }
    case SpvOpTypeRuntimeArray: return 0;
    case SpvOpTypeStruct:
    {
        // returns the size of the last member + the offset of the last member.
        // does not account for padding after the struct.

        if (t->members.size == 0)
            return 0;

        spirv_struct_type_member *last_member = t->members.data;
        u64 max_offset = 0;

        for_array(mem, &t->members)
        if (mem->offset > max_offset)
        {
            max_offset = mem->offset;
            last_member = mem;
        }

        if (max_offset > 0)
        {
            spirv_type *mem_type = _get_type_by_id(info, last_member->type_id);

            return calculate_type_size(mem_type, info) + last_member->offset;
        }
        else
        {
            u64 total_size = 0;
            for_array(mem2, &t->members)
            {
                spirv_type *mem_type = _get_type_by_id(info, mem2->type_id);
                total_size += mem_type->size;
            }

            return total_size;
        }
    }
    case SpvOpTypeOpaque:       return 0;
    case SpvOpTypePointer:      return 0;
    case SpvOpTypeFunction:     return 0;
    case SpvOpTypeEvent:        return 0;
    case SpvOpTypeDeviceEvent:  return 0;
    case SpvOpTypeReserveId:    return 0;
    case SpvOpTypeQueue:        return 0;
    case SpvOpTypePipe:         return 0;
    case SpvOpTypePipeStorage:  return 0;
    case SpvOpTypeNamedBarrier: return 0;
    default:
        return 0;
    }

    return 0;
}

void collect_type_information(spirv_info *info)
{
    // get type sizes
    for_array(t, &info->types)
        t->size = calculate_type_size(t, info);
}

void print_extra_type_information_inline(spirv_type *t, spirv_info *info, u32 depth)
{
    spirv_id_instruction *instr = t->instruction;

    switch (instr->opcode)
    {
    case SpvOpTypeVoid: printf("void"); break;
    case SpvOpTypeBool: printf("bool"); break;
    case SpvOpTypeInt:
    {
        u32 width = instr->words[2];
        u32 sign  = instr->words[3];

        if (sign)
            printf("s%d", width);
        else
            printf("u%d", width);
        break;
    }
    case SpvOpTypeFloat:
    {
        u32 width = instr->words[2];

        if (width > 32)
            printf("double");
        else
            printf("float");
        break;
    }
    case SpvOpTypeVector:
    {
        SpvId comp_id = (SpvId)t->instruction->words[2];
        spirv_type *comp_type = _get_type_by_id(info, comp_id);
        u32 comp_count = t->instruction->words[3];

        printf("vec%u<", comp_count);
        print_extra_type_information_inline(comp_type, info, depth+1);
        printf(">");
        break;
    }
    case SpvOpTypeMatrix:
    {
        SpvId vec_id = (SpvId)t->instruction->words[2];
        spirv_type *vec_type = _get_type_by_id(info, vec_id);
        u32 column_count = t->instruction->words[3];

        SpvId comp_id = (SpvId)vec_type->instruction->words[2];
        spirv_type *comp_type = _get_type_by_id(info, comp_id);
        u32 row_count = vec_type->instruction->words[3];

        printf("mat%ux%u<", row_count, column_count);
        print_extra_type_information_inline(comp_type, info, depth+1);
        printf(">");
        break;
    }
    case SpvOpTypeImage: printf("image"); break;
    case SpvOpTypeSampler: printf("sampler"); break;
    case SpvOpTypeSampledImage: printf("sampled_image"); break;
    case SpvOpTypeArray:
    {
        SpvId elem_type_id = (SpvId)t->instruction->words[2];
        spirv_type *elem_type = _get_type_by_id(info, elem_type_id);

        SpvId length_var_id = (SpvId)t->instruction->words[3];
        spirv_variable *length_var = _get_variable_by_id(info, length_var_id);

        // maybe handle larger types
        u32 length = length_var->instruction->words[3];
        
        print_extra_type_information_inline(elem_type, info, depth+1);
        printf("[%u]", length);

        break;
    }
    case SpvOpTypeRuntimeArray:
    {
        SpvId elem_type_id = (SpvId)t->instruction->words[2];
        spirv_type *elem_type = _get_type_by_id(info, elem_type_id);
        
        printf("array<");
        print_extra_type_information_inline(elem_type, info, depth+1);
        printf(">");

        break;
    }
    case SpvOpTypeStruct:
    {
        if (depth > 0)
        {
            printf("%s", t->instruction->name);
            break;
        }

        printf("struct %s\n{\n", t->instruction->name);

        for_array(mem, &t->members)
        {
            spirv_type *mem_type = _get_type_by_id(info, mem->type_id);
            printf("\t[offset %3lu, size %3lu]\t", mem->offset, mem_type->size);
            print_extra_type_information_inline(mem_type, info, depth+1);
            printf(" %s;\n", mem->name);
        }

        printf("}");
        break;
    }
    case SpvOpTypeOpaque:
    {
        const char *name = (const char *)t->instruction->words + 2;

        printf("%s", name);
    }
    case SpvOpTypePointer:
    {
        SpvStorageClass storage = (SpvStorageClass)t->instruction->words[2];
        SpvId type_id = (SpvId)t->instruction->words[3];
        spirv_type *type = _get_type_by_id(info, type_id);

        const char *storage_name = storage_class_name(storage);

        printf("%s ", storage_name);
        print_extra_type_information_inline(type, info, depth+1);
        printf("*");

        break;
    }
    case SpvOpTypeFunction: printf("function"); break;
    case SpvOpTypeEvent: printf("event"); break;
    case SpvOpTypeDeviceEvent: printf("device_event"); break;
    case SpvOpTypeReserveId: printf("reserve_id"); break;
    case SpvOpTypeQueue: printf("queue"); break;
    case SpvOpTypePipe: printf("pipe"); break;
    case SpvOpTypePipeStorage: printf("pipe_storage"); break;
    case SpvOpTypeNamedBarrier: printf("named_barrier"); break;
        break;
    }
}

void print_extra_type_information(spirv_info *info)
{
    for_array(t, &info->types)
    {
        printf("%%%u [size %3lu]\t= ", t->instruction->id, t->size);
        print_extra_type_information_inline(t, info, 0);
        printf("\n");
    }
}

void handle_spirv_op_type(u64 i, spirv_type *type, const spirv_info *info)
{
    u32 bound = (u32)info->id_instructions.size;
    spirv_id_instruction *id_instr = type->instruction;
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

        ::reserve(&type->members, member_count);
        type->members.size = member_count;

        for (u32 mem_i = 0; mem_i < member_count; ++mem_i)
        {
            SpvId mem_id = member_ids[mem_i];
            type->members[mem_i].type_id = mem_id;
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
    ::fill_memory(output->id_instructions.data, 0, output->id_instructions.size);

    for_array(i, _idinstr, &output->id_instructions)
    {
        init(_idinstr);
        _idinstr->id = (SpvId)i;
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

        // ::copy_memory(instr, id_instr, sizeof(spirv_instruction));

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

    // we remember member name indices for debug information instructions
    // because at this point there are no types / members to write to.
    array<u64> member_name_idxs{};
    defer { free(&member_name_idxs); };

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

            u32 member = instr->words[2];
            const char *member_name = (const char *)(instr->words + 3);
            printf(INSTR_FMT " OpMemberName %%%u %u \"%s\"\n", i, id, member, member_name);

            ::add_at_end(&member_name_idxs, i);

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

    printf("\nDecorations\n");

    // once again, since types are defined later (thanks khronos), we
    // have to remember the member decoration indices and handle them
    // later.
    array<u64> member_decor_idxs{};
    defer { free(&member_decor_idxs); };

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

            spirv_id_instruction *target_instr = output->id_instructions.data + target_id;
            ::insert_element(&target_instr->decoration_indices, (u32)output->decorations.size-1);

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
            ::add_at_end(&member_decor_idxs, i);
            
            SpvId target_type_id = (SpvId)instr->words[1];
            assert(target_type_id < bound);

            spirv_id_instruction *target_instr = output->id_instructions.data + target_type_id;
            ::insert_element(&target_instr->decoration_indices, (u32)output->decorations.size-1);

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

            spirv_id_instruction *target_instr = output->id_instructions.data + target_id;
            ::insert_element(&target_instr->decoration_indices, (u32)output->decorations.size-1);

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

    printf("\nTypes\n");

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
            spirv_type *t = ::add_at_end(&output->types);
            ::init(t);
            t->instruction = id_instr;
            t->size = UNCALCULATED;
            id_instr->extra = output->types.size - 1; // index of the type in the types array...

            handle_spirv_op_type(i, t, output);
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
            spirv_variable *var = ::add_at_end(&output->variables);
            var->instruction = id_instr;
            id_instr->extra = output->variables.size - 1;

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

    // take care of member debug info
    for_array(_mem_idx, &member_name_idxs)
    {
        instr = instructions.data + *_mem_idx;

        assert(instr->word_count >= 4);
        SpvId id = (SpvId)instr->words[1];
        assert(id < bound);

        u32 member = instr->words[2];
        const char *member_name = (const char *)(instr->words + 3);

        spirv_id_instruction *idinstr = output->id_instructions.data + id;
        assert(idinstr->extra < output->types.size);
        spirv_type *type = output->types.data + idinstr->extra;

        if (member >= type->members.reserved_size)
            ::reserve(&type->members, member + 4);

        if (member >= type->members.size)
            type->members.size = member + 1;

        type->members[member].name = member_name;
        type->members[member].offset = 0;
    }

    // take care of member decorations
    for_array(_mem_dec_idx, &member_decor_idxs)
    {
        instr = instructions.data + *_mem_dec_idx;

        SpvId target_type_id = (SpvId)instr->words[1];

        spirv_id_instruction *idinstr = output->id_instructions.data + target_type_id;
        spirv_type *type = output->types.data + idinstr->extra;

        u32 member = instr->words[2];

        if (member >= type->members.reserved_size)
            ::reserve(&type->members, member + 4);

        if (member >= type->members.size)
            type->members.size = member + 1;

        SpvDecoration decoration = (SpvDecoration)instr->words[3];

        switch (decoration)
        {
        case SpvDecorationOffset:
        {
            u32 offset = instr->words[4];
            type->members[member].offset = offset;
            break;
        }

        default:
            break;
        }
    }

    printf("\nFunctions\n");

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

        spirv_id_instruction *id_instr = output->id_instructions.data + result_id;
        ::copy_memory(instr, id_instr, sizeof(spirv_instruction));

        u32 func_index = (u32)output->functions.size;
        spirv_function *func = ::add_at_end(&output->functions);
        init(func);
        func->instruction = id_instr;

        for_array(ep, &output->entry_points)
        if (id_instr == ep->instruction)
        {
            ep->function_index = func_index;
            break;
        }

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
                assert(finstr->word_count == 3);

                SpvId result_type_id = (SpvId)finstr->words[1];
                assert(result_type_id < bound);

                SpvId result_id = (SpvId)finstr->words[2];
                assert(result_id < bound);

                printf(INSTR_ID_FMT " OpFunctionParameter %%%u\n", i, result_id, result_type_id);

                break;
            }
            case SpvOpLabel:
            {
                printf(INSTR_FMT " OpLabel\n", i);
                break;
            }
            case SpvOpAccessChain:
            {
                assert(finstr->word_count >= 4);

                SpvId result_type_id = (SpvId)finstr->words[1];
                assert(result_type_id < bound);

                SpvId result_id = (SpvId)finstr->words[2];
                assert(result_id < bound);

                SpvId base_id = (SpvId)finstr->words[3];
                assert(base_id < bound);

                printf(INSTR_ID_FMT " OpAccessChain %%%u %%%u", i, result_id, result_type_id, base_id);

                for (u32 ac = 4; ac < finstr->word_count; ++ac)
                {
                    SpvId index_id = (SpvId)finstr->words[ac];
                    assert(index_id < bound);

                    printf(" %%%u", index_id);
                }

                printf("\n");

                break;
            }
            case SpvOpLoad:
            {
                assert(finstr->word_count >= 4);

                SpvId result_type_id = (SpvId)finstr->words[1];
                assert(result_type_id < bound);

                SpvId result_id = (SpvId)finstr->words[2];
                assert(result_id < bound);

                SpvId ptr_id = (SpvId)finstr->words[3];
                assert(ptr_id < bound);

                printf(INSTR_ID_FMT " OpLoad %%%u %%%u", i, result_id, result_type_id, ptr_id);

                for (u32 load = 4; load < finstr->word_count; ++load)
                {
                    SpvMemoryAccessMask msk = (SpvMemoryAccessMask)finstr->words[load];

                    printf(" %d", msk);
                }

                printf("\n");

                break;
            }
            case SpvOpReturn:
            {
                printf(INSTR_FMT " OpReturn\n", i);
                break;
            }
            case SpvOpFunctionEnd:
            {
                assert(finstr->word_count == 1);

                printf(INSTR_FMT " OpFunctionEnd\n", i);

                breakout = true;
                break;
            }

            // TODO: handle branches / other functions

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

    for_array(ep, &output->entry_points)
    if (ep->function_index == max_value(u32))
    {
        get_spirv_parse_error(err, "entry point %s has no function\n", ep->name);
        return false;
    }

    printf("\nExtra function information\n");

    collect_function_information(output);

    printf("\nExtra type information\n");

    collect_type_information(output);

    // print_extra_type_information(output);

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

// utility
u64 get_indirect_type_size(SpvId type_id, spirv_info *info)
{
    spirv_type *t = _get_type_by_id(info, type_id);

    if (t == nullptr)
        return 0;

    if (t->instruction->opcode == SpvOpTypePointer)
        return get_indirect_type_size((SpvId)t->instruction->words[3], info);

    return t->size;
}

VkDescriptorType get_descriptor_type_by_spirv_type(SpvId type_id, spirv_info *info, SpvStorageClass storage = (SpvStorageClass)max_value(int))
{
    spirv_type *t = _get_type_by_id(info, type_id);

    if (t == nullptr)
        return (VkDescriptorType)max_value(int);

    switch (t->instruction->opcode)
    {
    case SpvOpTypePointer:
    {
        SpvStorageClass s = (SpvStorageClass)t->instruction->words[2];
        SpvId rtype_id = (SpvId)t->instruction->words[3];

        return get_descriptor_type_by_spirv_type(rtype_id, info, s);
    }
    case SpvOpTypeBool:
    case SpvOpTypeInt:
    case SpvOpTypeFloat:
    case SpvOpTypeVector:
    case SpvOpTypeMatrix:
    case SpvOpTypeArray:
    case SpvOpTypeRuntimeArray:
    case SpvOpTypeStruct:
    {
        switch (storage)
        {
        case SpvStorageClassUniform:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case SpvStorageClassStorageBuffer:  return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        default:                            return (VkDescriptorType)max_value(int);
        }

        break;
    }
    case SpvOpTypeImage:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case SpvOpTypeSampler:      return VK_DESCRIPTOR_TYPE_SAMPLER;
    case SpvOpTypeSampledImage: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    default:
        return (VkDescriptorType)max_value(int);
    }
}

void init(spirv_descriptor_set *dset)
{
    ::init(&dset->layout_bindings);
}

void free(spirv_descriptor_set *dset)
{
    ::free(&dset->layout_bindings);
}

void init(spirv_pipeline_info *info)
{
    ::init(&info->descriptor_sets);
    ::init(&info->push_constants);
}

void free(spirv_pipeline_info *info)
{
    ::free<true>(&info->descriptor_sets);
    ::free(&info->push_constants);
}

VkShaderStageFlags execution_model_to_shader_stage_flags(SpvExecutionModel model)
{
    if (model >= SpvExecutionModelKernel)
        return 0;

    return (VkShaderStageFlags)(1 << (int)(model));
}

void get_pipeline_info(spirv_pipeline_info *out, spirv_info *info)
{
    for_array(ep, &info->entry_points)
    {
        spirv_function *func = info->functions.data + ep->function_index;
        VkShaderStageFlags stage_flags = execution_model_to_shader_stage_flags(ep->execution_model);

        for_array(_var, &func->referenced_variables)
        {
            spirv_id_instruction *var_instr = (*_var)->instruction;
            SpvId result_type_id = (SpvId)var_instr->words[1];

            if (var_instr->opcode == SpvOpVariable
             && ((SpvStorageClass)var_instr->words[3] == SpvStorageClassPushConstant))
            {
                VkPushConstantRange *range = ::add_at_end(&out->push_constants);
                range->stageFlags = stage_flags;
                range->offset = 0; // TODO: find out the offset???
                range->size = get_indirect_type_size(result_type_id, info);
                continue;
            }

            u32 dset = max_value(u32);
            u32 binding = max_value(u32);

            for_array(idx, &var_instr->decoration_indices)
            {
                spirv_instruction *decor_instr = info->decorations.data + (*idx);

                if (decor_instr->opcode != SpvOpDecorate)
                    continue;

                SpvDecoration decoration = (SpvDecoration)decor_instr->words[2];

                switch (decoration)
                {
                case SpvDecorationBinding:
                {
                    binding = decor_instr->words[3];
                    break;
                }
                case SpvDecorationDescriptorSet:
                {
                    dset = decor_instr->words[3];
                    break;
                }
                default:
                    continue;
                }

                if (dset != max_value(u32) && binding != max_value(u32))
                    break;
            }

            printf("%u %u\n", dset, binding);

            if (dset == max_value(u32) || binding == max_value(u32))
                continue;

            if (dset >= out->descriptor_sets.size)
            {
                ::reserve(&out->descriptor_sets, dset + 4);

                for (u64 _di = out->descriptor_sets.size; _di <= dset; ++_di)
                    ::init(out->descriptor_sets.data + _di);

                out->descriptor_sets.size = dset + 1;
            }

            spirv_descriptor_set *sds = out->descriptor_sets.data + dset;

            if (binding >= sds->layout_bindings.size)
            {
                ::reserve(&sds->layout_bindings, binding + 4);

                u64 cursize = sds->layout_bindings.size;
                u64 diff = (binding + 1) - cursize;
                ::fill_memory(sds->layout_bindings.data + cursize, 0, diff);

                sds->layout_bindings.size = binding + 1;
            }

            VkDescriptorSetLayoutBinding *lb = sds->layout_bindings.data + binding;
            lb->binding = binding;
            lb->descriptorCount = 1; // TODO: implement descriptor count
            lb->stageFlags |= stage_flags;
            lb->pImmutableSamplers = nullptr;
            lb->descriptorType = get_descriptor_type_by_spirv_type(result_type_id, info);
        }
    }
}
