
#include "spirv1_2.h"

#include "shl/array.hpp"
#include "shl/error.hpp"
#include "shl/streams.hpp"

struct spirv_instruction
{
    u32 *words;
    u16 word_count;
    u16 opcode;
};

struct spirv_struct_type_member
{
    SpvId type_id;
    const char *name;
};

struct spirv_id_instruction : public spirv_instruction
{
    SpvId id;
    const char *name;

    array<spirv_struct_type_member> members;
};

void init(spirv_id_instruction *idinstr);
void free(spirv_id_instruction *idinstr);

struct spirv_entry_point_execution_mode
{
    SpvExecutionMode execution_mode;
    u32 *words;
    u16 word_count;
};

struct spirv_entry_point
{
    spirv_id_instruction *instruction;
    SpvExecutionModel execution_model; // i.e. the stage
    const char *name; // name in OpEntryPoint, probably same as in OpName

    SpvId *refs; 
    u16 ref_count;

    array<spirv_entry_point_execution_mode> execution_modes;
};

void init(spirv_entry_point *ep);
void free(spirv_entry_point *ep);

struct spirv_type
{
    spirv_id_instruction *instruction;
};

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

    SpvAddressingModel addressing_model;
    SpvMemoryModel memory_model;

    memory_stream data;
};

void init(spirv_info *info);
void free(spirv_info *info);

spirv_entry_point *get_entry_point_by_id(spirv_info *info, SpvId id);

bool parse_spirv_from_memory(memory_stream *input, spirv_info *output, error *err);
bool parse_spirv_from_file(const char *file, spirv_info *output, error *err);

