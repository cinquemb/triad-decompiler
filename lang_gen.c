#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libdis.h>

#include "lang_gen.h"
#include "datastructs.h"

char test_conditions [14] [3] = {"<\0\0", ">=\0", "!=\0", "==\0", "<=\0", ">\0\0", "<\0\0", ">\0\0", "\0\0\0", "\0\0\0", "<\0\0", ">=\0", "<=\0", ">\0\0"};

void disassemble_insn (x86_insn_t instruction)
{
	char line [128];
	unsigned int target_addr;

	printf ("%p:", index_to_addr (instruction.addr));

	if (instruction.type == insn_jmp || instruction.type == insn_jcc || instruction.type == insn_call)
	{
		target_addr = relative_insn (&instruction, index_to_addr (instruction.addr) + instruction.size);
		printf ("\t%s\t%p\n", instruction.mnemonic, target_addr);
	}
	else
	{
		x86_format_insn (&instruction, line, 128, att_syntax);
		printf ("\t%s\n", line);
	}
}

void decompile_insn (x86_insn_t instruction, x86_insn_t next_instruction, jump_block* parent)
{
	char* line = malloc (128);
	Elf32_Sym* name_sym = NULL;
	char* name_string = NULL;
	int len;
	int line_length = 128;
	var* temp = NULL;
	var* temp2 = NULL;
	bzero (line, 128);
	int actual_translation_size = strlen (translation);
	int i;
	int target_addr;

	if (instruction.type >> 12 == insn_arithmetic || instruction.type >> 12 == insn_logic || instruction.type >> 12 == insn_move)
	{
		temp = add_var (instruction.operands->op);
		if (instruction.operands->next)
			temp2 = add_var (instruction.operands->next->op);
	}
	if (!temp || (strcmp (temp->name, "ebp") && strcmp (temp->name, "esp")))
	{
		switch (instruction.type)
		{
			//We cut out anything involving EBP or ESP since these are not general purpose registers and would not be part of original program arithmetic
			//insn_mov through insn_xor are just basic arithmetic operators
			case insn_mov:
				if (strcmp (instruction.mnemonic, "lea"))
				{
					if (temp->type != DEREF && temp2->type != DEREF)
						sprintf (line, "%s = %s;\n", temp->name, temp2->name);
					else if (temp->type == DEREF && temp2->type != DEREF)
						sprintf (line, "*(%s*)(%s+(%d)) = %s;\n", temp->c_type, temp->name, temp->loc.disp, temp2->name);
					else if (temp->type != DEREF && temp2->type == DEREF)
						sprintf (line, "%s = *(%s*)(%s+(%d));\n", temp->name, temp2->c_type, temp2->name, temp2->loc.disp);
					else
						sprintf (line, "*(%s*)(%s+(%d)) = *(%s*)(%s+(%d))", temp->c_type, temp->name, temp->loc.disp, temp2->c_type, temp2->name, temp2->loc.disp);
				}
				else
					sprintf (line, "%s = (%s)&%s;\n", temp->name, temp2->c_type, temp2->name);
				break;
			case insn_sub:
				sprintf (line, "%s -= %s;\n", temp->name, temp2->name);
				break;
			case insn_add:
				sprintf (line, "%s += %s;\n", temp->name, temp2->name);
				break;
			case insn_mul:
				sprintf (line, "%s *= %s;\n", temp->name, temp2->name);
				break;
			case insn_div:
				sprintf (line, "%s /= %s;\n", temp->name, temp2->name);
				break;
			case insn_and:
				sprintf (line, "%s &= %s;\n", temp->name, temp2->name);
				break;
			case insn_or:
				sprintf (line, "%s |= %s;\n", temp->name, temp2->name);
				break;
			case insn_shr:
				sprintf (line, "%s = %s >> %s;\n", temp->name, temp->name, temp2->name);
				break;
			case insn_shl:
				sprintf (line, "%s = %s << %s;\n", temp->name, temp->name, temp2->name);
				break;
			case insn_xor:
				sprintf (line, "%s ^= %s;\n", temp->name, temp2->name);
				break;
			case insn_not:
				sprintf (line, "%s = ~%s;\n", temp->name, temp->name);
				break;
			case insn_dec:
				sprintf (line, "%s --;\n", temp->name);
				break;
			case insn_inc:
				sprintf (line, "%s ++;\n", temp->name);
				break;
			case insn_pop:
				break;
			case insn_return:
				sprintf (line, "return eax;\n"); //All functions return EAX
				break;
			case insn_leave:
				break;
			case insn_push: //pushing onto the stack is how the caller passes arguments to the the callee
				temp = add_var (instruction.operands->op);
				if (temp->type != REG || (strcmp (temp->name, "ebp") && strcmp (temp->name, "esp") && strcmp (temp->name, "ecx")))
				{
					//Add the variable to the argument array (caller_param). Cannot use argument linked list because the variables used are already linked into the local
					//variable linked list.
					if (!caller_param)
					{
						caller_params_size = 8*sizeof (var);
						caller_param = malloc (caller_params_size);
					}
					num_caller_params ++;
					if (num_caller_params * sizeof (var) > caller_params_size)
					{
						caller_params_size *= 2;
						caller_param = realloc (caller_param, caller_params_size);
					}
					caller_param [num_caller_params-1] = *temp;
					
				}
				break;
			case insn_call:
				target_addr = relative_insn (&instruction, index_to_addr (instruction.addr) + instruction.size);
				if (addr_to_index (target_addr) < executable_segment_size && (unsigned char)file_buf [addr_to_index (target_addr)] == 0xFF && dynamic_string_table)
				{
					name_sym = find_reloc_sym (*(int*)&(file_buf [addr_to_index (target_addr)+2]));
					if (name_sym)
						name_string = dynamic_string_table + name_sym->st_name;
				}
				else
				{
					name_sym = find_sym (symbol_table, symbol_table_end, target_addr);
					if (name_sym)
						name_string = string_table + name_sym->st_name;
				}
				if (name_string)
				{
					len = strlen (name_string);
					if (len + 12 + num_caller_params*22 > line_length)
					{
						line_length = len + 12 + num_caller_params*22;
						line = realloc (line, line_length);
					}
					sprintf (line, "eax = %s (", name_string);
				}
				else
					sprintf (line, "eax = func_%p (", target_addr);
	
				//Print the argument list
				if (caller_param)
				{
					sprintf (&(line [strlen (line)]), "%s", caller_param [num_caller_params-1].name);
					for (i = num_caller_params-2; i >= 0; i --)
					{
						if (caller_param [i].type == DEREF)
							sprintf (&(line [strlen (line)]), ", *(%s*)(%s+(%d))", caller_param [i].c_type, caller_param [i].name, caller_param [i].loc.disp);
						else
							sprintf (&(line [strlen (line)]), ", %s", caller_param [i].name);
					}
				}

				sprintf (&(line [strlen (line)]), ");\n");
				free (caller_param);
				caller_param = NULL;
				num_caller_params = 0;
				caller_params_size = 0;
				break;
			case insn_test: //the test instruction is normally found in the context test %eax,%eax. This compares EAX to 0.
				//Instruction after a compare or a test is usually a conditional jump
				target_addr = relative_insn (&next_instruction, index_to_addr (next_instruction.addr) + next_instruction.size); 
				temp = add_var (instruction.operands->op);
				if (language_flag == 'f')
				{
					if (parent->flags & IS_WHILE)
						sprintf (next_line, "while (%s %s 0)\n", temp->name, test_conditions [next_instruction.bytes [0] - 0x72]);
					else if (target_addr > index_to_addr (next_instruction.addr))
						sprintf (next_line, "if (%s %s 0)\n", temp->name, test_conditions [next_instruction.bytes [0] - 0x72]); //The conditional jumps start with "jump if below," which has an opcode of 0x72
					else
					{
						sprintf (next_line, "} while (%s %s 0);\n", temp->name, test_conditions [next_instruction.bytes [0] - 0x72]);
						num_tabs -= 2;
					}
					num_tabs ++;
				}
				else
					sprintf (line, "%s %s, %s\n", instruction.mnemonic, temp->name, temp->name);

				break;
			case insn_cmp: //the compare instructions just "compares" its two operands
				temp = add_var (instruction.operands->op);
				temp2 = add_var (instruction.operands->next->op);
				if (language_flag == 'f')
				{
					target_addr = relative_insn (&next_instruction, index_to_addr (next_instruction.addr) + next_instruction.size);
					if (parent->flags & IS_WHILE)
						sprintf (next_line, "while (%s %s %s)\n", temp->name, test_conditions [next_instruction.bytes [0] - 0x72], temp2->name);
					else if (target_addr > index_to_addr (next_instruction.addr))
						sprintf (next_line, "if (%s %s %s)\n", temp->name, test_conditions [next_instruction.bytes [0] - 0x72], temp2->name);
					else
					{
						sprintf (next_line, "} while (%s %s %s);\n", temp->name, test_conditions [next_instruction.bytes [0] - 0x72], temp2->name);
						num_tabs -= 2;
					}
					num_tabs ++;
				}
				else
					sprintf (line, "%s %s, %s\n", instruction.mnemonic, temp->name, temp2->name);

				break;
			case insn_jcc:
				if (language_flag == 'f')
				{
					sprintf (line, next_line);
					bzero (next_line, 128);
				}
				else
				{
					target_addr = relative_insn (&instruction, index_to_addr (instruction.addr) + instruction.size);
					sprintf (line, "%s %p\n", instruction.mnemonic, target_addr);
				}

				break;
			case insn_jmp:
				if (language_flag != 'f')
				{
					target_addr = relative_insn (&instruction, index_to_addr (instruction.addr) + instruction.size);
					sprintf (line, "%s %p\n", instruction.mnemonic, target_addr);
				}
				break;
			case insn_nop:
				break;
			default:
				x86_format_insn (&instruction, line, 128, att_syntax);
				line [strlen (line)] = '\n';
				break;
		}
	}

	//Add the translated line to the final translation
	if (actual_translation_size + strlen (line) + num_tabs > translation_size)
	{
		translation_size = 2*(translation_size + strlen (line) + num_tabs);
		translation = realloc (translation, translation_size);
	}
	if (language_flag == 'f')
	{
		if (line [0] && (instruction.type != insn_jcc || line [0] == '}'))
		{
			for (i = 0; i < num_tabs; i ++)
			{
				sprintf (&(translation [actual_translation_size]), "\t");
				actual_translation_size ++;
			}
		}
		else if (instruction.type == insn_jcc && line [0] != '}')
		{
			for (i = 0; i < num_tabs-1; i ++)
			{
				sprintf (&(translation [actual_translation_size]), "\t");
				actual_translation_size ++;
			}
		}
	}
	else if ((!temp || (strcmp (temp->name, "ebp") && strcmp (temp->name, "esp"))) && instruction.type != insn_push && instruction.type != insn_pop && instruction.type != insn_leave && instruction.type != insn_nop)
	{
		sprintf (&(translation [actual_translation_size]), "\t");
		actual_translation_size ++;
	}
	strcpy (&(translation [actual_translation_size]), line);
	if (language_flag == 'f')
	{
		if (instruction.type == insn_jcc && line [0] != '}')
		{
			actual_translation_size = strlen (translation);
			for (i = 0; i < num_tabs - 1; i ++)
			{
				sprintf (&(translation [actual_translation_size]), "\t");
				actual_translation_size ++;
			}
			sprintf (&(translation [actual_translation_size]), "{\n");
		}
	}
		
	actual_translation_size += strlen (line);
	translation [actual_translation_size] = '\0';

	free (line);
}

void disassemble_jump_block (jump_block* to_translate)
{
	int i;

	//Translate every instruction contained in jump block
	for (i = 0; i < to_translate->num_instructions; i ++)
		disassemble_insn (to_translate->instructions [i]);
}

void partial_decompile_jump_block (jump_block* to_translate, function* parent)
{
	int i;
	int j;
	int len;

	for (i = 0; i < parent->num_jump_addrs; i ++)
	{
		if (to_translate->next->start == parent->jump_addrs [i])
		{
			len = strlen (translation);
			if (len + 12 > translation_size)
			{
				translation_size  = 2*(len + 12);
				translation = realloc (translation, translation_size);
			}
			sprintf (&(translation [len]), "%p:\n", to_translate->next->start);

			for (j = 0; j < parent->num_jump_addrs; j ++)
			{
				if (to_translate->next->start == parent->jump_addrs [j])
					parent->jump_addrs [j] = 0;
			}
		}
	}

	//Translate every instruction contained in jump block
	for (i = 0; i < to_translate->num_instructions; i ++)
		decompile_insn (to_translate->instructions [i], to_translate->instructions [i+1], to_translate);
}

//Final translation of each jump block, among other things.
//Also contains some routines that need to be performed per jump block.
//These routines are second iteration routines (see jump_block_preprocessing header)
void decompile_jump_block (jump_block* to_translate, function* parent)
{
	int i;
	int j;
	int len;
	unsigned int target = 0;
	unsigned int target2 = 0;

	if (to_translate->instructions [to_translate->num_instructions-1].type == insn_jmp) //Get unconditional jump target address, if possible
	{
		target = to_translate->instructions [to_translate->num_instructions-1].size + index_to_addr (to_translate->instructions [to_translate->num_instructions-1].addr);
		target = relative_insn (&(to_translate->instructions [to_translate->num_instructions-1]), target);
	}
	if (to_translate->instructions [to_translate->num_instructions-1].type == insn_jcc) //Get conditional jump target address, if possible
	{
		target2 = to_translate->instructions [to_translate->num_instructions-1].size + index_to_addr (to_translate->instructions [to_translate->num_instructions-1].addr);
		target2 = relative_insn (&(to_translate->instructions [to_translate->num_instructions-1]), target2);
	}
	unsigned int orig = index_to_addr (to_translate->instructions [to_translate->num_instructions-1].addr); //Get address of last instruction in block

	//Need to print out a "}" at the end of a non-nested IF/ELSE statement, so we need to override the "do not place } at an unconditional jump address" rule.
	if (to_translate->next && target && to_translate->next->flags & IS_ELSE)
		file_buf [to_translate->instructions [to_translate->num_instructions-1].addr] = 0xea;

	//Sets up pivot addresses for "}" placement algorithm.
	//Pivot addresses is the conditional jump of the IF statement associated with the ELSE right before the duplicated target
	if (to_translate->next && to_translate->next->flags & IS_IF)
	{
		for (i = 0; i < parent->num_dups; i ++)
		{
			if (target2 && target2 == parent->else_starts [i])
			{
				parent->pivots [i] = orig;
				break;
			}
		}
	}

	//"}" placement algorithm
	if (target || target2)
	{
		for (i = 0; i < parent->num_dups; i ++)
		{
			if (target2 && target2 == parent->dup_targets [i])
			{
				//Any jump that comes after the associated if statement, before the else statement, and isn't an unconditional jump immediately before the else 
				//block should be redirected to the start of the else block in order to get the rest of decompile_jump_blocks to place the "}" correctly
				if (orig >= parent->pivots [i] && orig < parent->else_starts [i] && parent->pivots [i] && to_translate->end != parent->else_starts [i])
				{
					if (to_translate->next->end != parent->dup_targets [i])
					{
						for (j = 0; j < parent->num_jump_addrs; j++)
						{
							if (parent->jump_addrs [j] == parent->dup_targets [i])
							{
								parent->jump_addrs [j] = parent->else_starts [i];
								break;
							}
						}
					}
				}
				break;
			}
			else if (target && target == parent->dup_targets [i])
			{
				if (orig >= parent->pivots [i] && orig < parent->else_starts [i] && parent->pivots [i] && to_translate->end != parent->else_starts [i])
				{
					if (to_translate->next->end != parent->dup_targets [i])
					{
						for (j = 0; j < parent->num_jump_addrs; j++)
						{
							if (parent->jump_addrs [j] == parent->dup_targets [i])
							{
								parent->jump_addrs [j] = parent->else_starts [i];
								break;
							}
						}
					}
				}
				break;
			}
		}
	}

	//From here on out, code in this function will ACTUALLY append the translation to the end of the "translation" string.
	for (i = 0; i < parent->num_jump_addrs; i ++)
	{
		if (to_translate->start == parent->jump_addrs [i])
		{
			if (parent->orig_addrs [i])
			{
				if ((unsigned char)(file_buf [addr_to_index (parent->orig_addrs [i])]) != 0xeb) //0xeb is the unconditional jump opcode. Don't put a "}" or a "do" inside of a while loop TODO: this is dumb, find a different way to check jumps
				{
					len = strlen (translation);
					if (to_translate->start > parent->orig_addrs [i])
					{
						if (len + 2 + num_tabs > translation_size)
						{
							translation_size  = 2*(len + 2 + num_tabs);
							translation = realloc (translation, translation_size);
						}
						num_tabs --;
						for (j = 0; j < num_tabs; j ++)
						{
							sprintf (&(translation [len]), "\t");
							len ++;
						}
						sprintf (&(translation [len]), "}\n");
					}
				}
			}	
		}
	}

	for (i = 0; i < parent->num_jump_addrs; i ++)
	{
		if (to_translate->start == parent->jump_addrs [i])
		{
			if (parent->orig_addrs [i])
			{
				if ((unsigned char)(file_buf [addr_to_index (parent->orig_addrs [i])]) != 0xeb) //0xeb is the unconditional jump opcode
				{
					len = strlen (translation);
					if (to_translate->start < parent->orig_addrs [i])
					{
						if (len + 6 + 2*num_tabs > translation_size)
						{
							translation_size  = 2*(len + 6 + 2*num_tabs);
							translation = realloc (translation, translation_size);
						}
						for (j = 0; j < num_tabs; j ++)
						{
							sprintf (&(translation [len]), "\t");
							len ++;
						}
						sprintf (&(translation [len]), "do\n");
						len += 3;
						for (j = 0; j < num_tabs; j ++)
						{
							sprintf (&(translation [len]), "\t");
							len ++;
						}
						sprintf (&(translation [len]), "{\n");
						num_tabs ++;
					}
				}
			}
		}
	}

	if (to_translate->flags & IS_ELSE)
	{
		len = strlen (translation);
		if (len + 7 + 2*num_tabs > translation_size)
		{
			translation_size  = 2*(len + 7 + 2*num_tabs);
			translation = realloc (translation, translation_size);
		}
		for (i = 0; i < num_tabs; i ++)
		{
			sprintf (&(translation [len]), "\t");
			len ++;
		}
		sprintf (&(translation [len]), "else\n");
		len += 5;
		for (i = 0; i < num_tabs; i ++)
		{
			sprintf (&(translation [len]), "\t");
			len ++;
		}
		sprintf (&(translation [len]), "{\n");
		num_tabs ++;
	}	

	if (to_translate->flags & NO_TRANSLATE)
		return;

	//Translate every instruction contained in jump block
	for (i = 0; i < to_translate->num_instructions; i ++)
		decompile_insn (to_translate->instructions [i], to_translate->instructions [i+1], to_translate);

	if (to_translate->flags & IS_BREAK)
	{
		len = strlen (translation);
		if (len + 7 + num_tabs > translation_size)
		{
			translation_size  = 2*(len + 7 + num_tabs);
			translation = realloc (translation, translation_size);
		}
		for (i = 0; i < num_tabs; i ++)
		{
			sprintf (&(translation [len]), "\t");
			len ++;
		}
		sprintf (&(translation [len]), "break;\n");
	}
	else if (to_translate->flags & IS_CONTINUE)
	{
		len = strlen (translation);
		if (len + 10 + num_tabs > translation_size)
		{
			translation_size  = 2*(len + 10 + num_tabs);
			translation = realloc (translation, translation_size);
		}
		for (i = 0; i < num_tabs; i ++)
		{
			sprintf (&(translation [len]), "\t");
			len ++;
		}
		sprintf (&(translation [len]), "continue;\n");
	}
	else if (to_translate->flags & IS_GOTO)
	{
		len = strlen (translation);
		if (len + 17 + num_tabs > translation_size)
		{
			translation_size  = 2*(len + 18 + num_tabs);
			translation = realloc (translation, translation_size);
		}
		for (i = 0; i < num_tabs; i ++)
		{
			sprintf (&(translation [len]), "\t");
			len ++;
		}
		sprintf (&(translation [len]), "goto %p;\n", target);
	}

	for (i = 0; i < parent->num_jump_addrs; i ++)
	{
		if (to_translate->next->start == parent->jump_addrs [i] && !(parent->orig_addrs [i]))
		{
			len = strlen (translation);
			if (len + 12 > translation_size)
			{
				translation_size  = 2*(len + 12);
				translation = realloc (translation, translation_size);
			}
			sprintf (&(translation [len]), "%p:\n", to_translate->next->start);
		}
	}
}

//Jump block "preprocessing"
//Some per jump block routines require other routines to have been performed on EVERY jump block before they can be called.
//So we put first iteration routines in this function
void jump_block_preprocessing (jump_block* to_process, function* parent)
{
	int i = 0;
	int j = 0;
	unsigned int target = 0;
	unsigned int target2 = 0;
	if (to_process->instructions [to_process->num_instructions-1].type == insn_jmp)
	{
		target = to_process->instructions [to_process->num_instructions-1].size + index_to_addr (to_process->instructions [to_process->num_instructions-1].addr);
		target = relative_insn (&(to_process->instructions [to_process->num_instructions-1]), target);
	}
	if (to_process->instructions [to_process->num_instructions-1].type == insn_jcc)
	{
		target2 = to_process->instructions [to_process->num_instructions-1].size + index_to_addr (to_process->instructions [to_process->num_instructions-1].addr);
		target2 = relative_insn (&(to_process->instructions [to_process->num_instructions-1]), target2);
	}
	unsigned int orig = index_to_addr (to_process->instructions [to_process->num_instructions-1].addr);

	if (target2)
		to_process->next->flags |= IS_IF;

	if (to_process->flags & IS_LOOP)
		to_process->next->flags |= IS_AFTER_LOOP;

	if (target || to_process->instructions [to_process->num_instructions-1].type == insn_jcc)
	{
		for (i = 0; i < parent->num_jump_addrs; i++)
		{
			if (parent->jump_addrs [i] == target && parent->orig_addrs [i] != orig) //This instruction has the same target address as another, therefore we have a duplicate jump target
			{
				for (j = 0; j < parent->num_dups; j ++)
				{
					if (parent->dup_targets [j] == target)
						break;
				}
				if (j == parent->num_dups)
				{
					parent->num_dups ++;

					//Memory management for "}" placement algorithms
					if (parent->num_dups * sizeof (unsigned int) >= parent->dup_targets_buf_size)
					{
						parent->dup_targets_buf_size *= 2;
						parent->dup_targets = realloc (parent->dup_targets, parent->dup_targets_buf_size);
						parent->else_starts = realloc (parent->else_starts, parent->dup_targets_buf_size);
						parent->pivots = realloc (parent->pivots, parent->dup_targets_buf_size);
					}

					//Document memory addresses that are the targets of multiple jump instructions
					parent->dup_targets [parent->num_dups-1] = target;
					parent->else_starts [parent->num_dups-1] = 0;
					parent->pivots [parent->num_dups-1] = 0;
				}
			}
		}
	}

	if (target2 && target2 > orig)
	{
		struct search_params params;
		jump_block* if_target;
		params.key = target2;
		params.ret = (void**)&if_target;
		list_loop (search_start_addrs, to_process, to_process, params);

		if_target->flags |= IS_IF_TARGET;
	}
	if (target) //Possibly a while loop (block ends in unconditional jump)
	{
		struct search_params params;
		jump_block* while_block;
		jump_block* new_block;
		x86_oplist_t* current1;
		x86_oplist_t* current2;		
		params.key = target;
		params.ret = (void**)&while_block;
		list_loop (search_start_addrs, to_process, to_process, params); //Search for the jump block targetted by this jump instructions
		unsigned int target3;
		target3 = while_block->instructions [while_block->num_instructions-1].size + index_to_addr (while_block->instructions [while_block->num_instructions-1].addr);
		target3 = relative_insn (&(while_block->instructions [while_block->num_instructions-1]), target3);

		if (while_block->flags & IS_AFTER_LOOP)
		{
			for (i = 0; i < parent->num_jump_addrs; i++)
			{
				if (parent->jump_addrs [i] == target && parent->orig_addrs [i] == orig)
					parent->jump_addrs [i] = 0;
			}
			to_process->flags |= IS_BREAK;
		}
		else if (while_block->flags & IS_LOOP && target3 <= orig)
		{
			for (i = 0; i < parent->num_jump_addrs; i++)
			{
				if (parent->jump_addrs [i] == target && parent->orig_addrs [i] == orig)
					parent->jump_addrs [i] = 0;
			}
			to_process->flags |= IS_CONTINUE;
		}
		else if (to_process->next->flags & IS_IF_TARGET && !(target && target < orig) && !(to_process->flags & IS_AFTER_ELSE))
		{
			to_process->next->flags |= IS_ELSE; //End of IF statement, and ELSE statement exists, so the next block should be the start of a ELSE statement

			//Keep track of else statements that come directly before addresses pointed to by multiple jump instructions (see "}" placement algorithm in decompile_jump_block)
			for (i = 0; i < parent->num_dups; i++)
			{
				if (target == parent->dup_targets [i])
				{
					parent->else_starts [i] = to_process->next->start; 
					break;
				}
			}

			while_block->flags |= IS_AFTER_ELSE;
		}
		else if (while_block->flags & IS_LOOP) //If this series of jump instructions ends in a backwards conditional jump, then this unconditional jump is the beginning of a while loop
		{
			new_block = malloc (sizeof (jump_block));
			*new_block = *while_block;
			link (to_process, new_block);
			new_block->instructions = malloc ((new_block->num_instructions+1) * sizeof (x86_insn_t));
			for (i = 0; i < new_block->num_instructions; i++) //Copy all instructions from while_block
			{
				new_block->instructions [i] = while_block->instructions [i];
				if (while_block->instructions [i].operand_count)
				{
					new_block->instructions [i].operands = malloc (sizeof (x86_oplist_t));
					current1 = new_block->instructions [i].operands;
					current2 = while_block->instructions [i].operands;
					current1->op = current2->op;
					current2 = current2->next;
					while (current2)
					{
						current1->next = malloc (sizeof (x86_oplist_t));
						current1 = current1->next;
						current1->op = current2->op;
						current2 = current2->next;
					}
					current1->next = NULL;
				}
			}
					
			new_block->instructions [i-1].bytes [0] = new_block->instructions [i-1].bytes [0];
			new_block->instructions [i-1].operands->op.type = op_absolute;
			new_block->instructions [i-1].operands->op.data.dword = while_block->end;
			new_block->instructions [i-1].operands->next = NULL;
			parent->num_jump_addrs ++;
			parent->jump_addrs [parent->num_jump_addrs-1] = while_block->end;
			parent->orig_addrs [parent->num_jump_addrs-1] = index_to_addr (to_process->instructions [i-1].addr);

			new_block->flags |= IS_WHILE;
			while_block->flags |= NO_TRANSLATE;
			for (i = 0; i < parent->num_jump_addrs; i++)
			{
				if (parent->orig_addrs [i] == index_to_addr (while_block->instructions [while_block->num_instructions-1].addr))
					parent->jump_addrs [i] = 0;
			}
		}
		else
		{
			for (i = 0; i < parent->num_jump_addrs; i++)
			{
				if (parent->jump_addrs [i] == target && parent->orig_addrs [i] == orig)
					parent->orig_addrs [i] = 0;
			}
			to_process->flags |= IS_GOTO;
		}
	}
}

void translate_func (function* to_translate)
{
	Elf32_Sym* func_name = NULL;

	//Disassemble the jump blocks again
	list_loop (parse_instructions, to_translate->jump_block_list, to_translate->jump_block_list);
	
	if (language_flag == 'd')
	{
		func_name = find_sym (symbol_table, symbol_table_end, to_translate->start_addr);
		if (func_name)
			printf ("int func_%p (%s)\n{\n", to_translate->start_addr, string_table + func_name->st_name);
		else
			printf ("int func_%p\n{\n", to_translate->start_addr);
		list_loop (disassemble_jump_block, to_translate->jump_block_list, to_translate->jump_block_list);
		printf ("}\n\n");
	}
	else
	{
		var* current_var;
		var* current_global = global_list;
		num_tabs = 1;

		while (current_global && current_global->next != global_list)
			current_global = current_global->next;

		//Reset variable finding state machine
		var_list = NULL;
		callee_param = NULL;
		caller_param = NULL;
		translation_size = 256;
		translation = malloc (translation_size);
		bzero (translation, translation_size);

		bzero (next_line, 128);

		//Translate all jump blocks in function
		if (language_flag == 'f')
		{
			list_loop (jump_block_preprocessing, to_translate->jump_block_list, to_translate->jump_block_list, to_translate);
			list_loop (decompile_jump_block, to_translate->jump_block_list, to_translate->jump_block_list, to_translate);
		}
		else
			list_loop (partial_decompile_jump_block, to_translate->jump_block_list, to_translate->jump_block_list, to_translate);

		if (current_global)
			list_loop (print_declarations, current_global, global_list, 0);
		else if (global_list)
			list_loop (print_declarations, global_list, global_list, 0);
		printf ("\n");

		//Print function header
		//NOTE: EAX will always be returned, what EAX means is up to the caller.
		//Since EAX is returned, a 32 bit int will always be returned
		func_name = find_sym (symbol_table, symbol_table_end, to_translate->start_addr);
		if (func_name)
			printf ("int %s (", string_table + func_name->st_name);
		else
			printf ("int func_%p (", to_translate->start_addr);
		current_var = callee_param;
		if (!callee_param)
			printf ("void)\n{\n");
		else
		{
			//Print parameter list
			printf ("%s %s", current_var->c_type, current_var->name);
			current_var = current_var->next;
			while (current_var != callee_param && current_var)
			{
				printf (", ");
				printf ("%s %s", current_var->c_type, current_var->name);
				current_var = current_var->next;
			}
			printf (")\n{\n");
		
		}

		//Print all variable declarations
		if (var_list)
		{
			list_loop (print_declarations, var_list, var_list, 1);
		}
		printf ("\n");

		//Print the string translation of the given instructions
		printf ("%s}\n\n", translation);
	}

	//Cleanup
	if (var_list)
		clean_var_list (var_list);
	if (callee_param)
		clean_var_list (callee_param);
	free (translation);
	free (to_translate->dup_targets);
	free (to_translate->else_starts);
	free (to_translate->pivots);
}

void translate_function_list (function* function_list)
{
	global_list = NULL;
	list_loop (translate_func, function_list, function_list);
	if (global_list)
		clean_var_list (global_list);
}

void print_declarations (var* to_print, char should_tab)
{
	if (to_print->type != DEREF && to_print->type != CONST && strcmp (to_print->name, "ebp") && strcmp (to_print->name, "esp")) //Dont need to declare constants. ESP and EBP are NOT general purpose
	{
		if (to_print->type == REG && should_tab)
			printf ("\tregister ");
		else if (to_print->type == REG)
			printf ("register ");
		if (should_tab && to_print->type != REG)
			printf ("\t%s %s", to_print->c_type, to_print->name);
		else
			printf ("%s %s", to_print->c_type, to_print->name);
		if (to_print->type != PARAM)
			printf (";\n");
	}
}
