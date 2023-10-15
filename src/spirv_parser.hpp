
#include "spirv1_2.h"

#include "shl/array.hpp"
#include "shl/error.hpp"
#include "shl/streams.hpp"

struct spirv_instruction;
struct spirv_id_instruction;
struct spirv_function;
struct spirv_entry_point_execution_mode;
struct spirv_entry_point;
struct spirv_struct_type_member;
struct spirv_type;
struct spirv_variable;
struct spirv_info;

struct spirv_instruction
{
    u32 *words;
    u16 word_count;
    u16 opcode;
    u32 extra; // meaning changes depending on opcode
};

struct spirv_id_instruction : public spirv_instruction
{
    SpvId id;
    const char *name;

    array<u32> decoration_indices; // indices into spirv_info->decorations
};

void init(spirv_id_instruction *instr);
void free(spirv_id_instruction *instr);

struct spirv_function
{
    spirv_id_instruction *instruction;

    array<u32> called_function_indices; // index into spirv_info->functions
    array<spirv_variable*> referenced_variables;
};

void init(spirv_function *func);
void free(spirv_function *func);

struct spirv_entry_point_execution_mode
{
    SpvExecutionMode execution_mode;
    u32 *words;
    u16 word_count;
};

struct spirv_entry_point
{
    spirv_id_instruction *instruction;
    u32 function_index; // index into spirv_info->functions
    SpvExecutionModel execution_model; // i.e. the stage
    const char *name; // name in OpEntryPoint, probably same as in OpName

    SpvId *refs; 
    u16 ref_count;

    array<spirv_entry_point_execution_mode> execution_modes;
};

void init(spirv_entry_point *ep);
void free(spirv_entry_point *ep);

struct spirv_struct_type_member
{
    SpvId type_id;
    const char *name;

    u64 offset;
};

struct spirv_type
{
    spirv_id_instruction *instruction;
    u64 size;

    array<spirv_struct_type_member> members;
};

void init(spirv_type *type);
void free(spirv_type *type);

struct spirv_variable
{
    spirv_id_instruction *instruction;
};

struct spirv_info
{
    array<spirv_id_instruction> id_instructions;
    array<spirv_entry_point> entry_points;
    array<spirv_type> types;
    array<spirv_variable> variables; // and constants
    array<spirv_instruction> decorations;

    array<spirv_function> functions;

    SpvAddressingModel addressing_model;
    SpvMemoryModel memory_model;

    memory_stream data;
};

void init(spirv_info *info);
void free(spirv_info *info);

spirv_entry_point *get_entry_point_by_id(spirv_info *info, SpvId id);

bool parse_spirv_from_memory(memory_stream *input, spirv_info *output, error *err);
bool parse_spirv_from_file(const char *file, spirv_info *output, error *err);

