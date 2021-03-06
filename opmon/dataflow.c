#include "cfg.h"
#include "compile_context.h"
#include "cfg_handler.h"
#include "dataflow.h"

#define DATAFLOW_VAR_NAME_THIS "this"
#define DATAFLOW_VAR_NAME_MISSING "<missing>"

void dump_script_header(application_t *app, const char *routine_name, uint function_hash)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  fprintf(oplog, " === %s (0x%x)\n", routine_name, function_hash);
}

void dump_function_header(application_t *app, const char *unit_path, const char *routine_name,
                          uint function_hash)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  fprintf(oplog, " === %s|%s (0x%x)\n", unit_path, routine_name, function_hash);
}

static bool uses_return_value(zend_op *op)
{
  switch (op->opcode) {
    case ZEND_ADD:
    case ZEND_SUB:
    case ZEND_MUL:
    case ZEND_DIV:
    case ZEND_MOD:
    case ZEND_SL:
    case ZEND_SR:
    case ZEND_CONCAT:
    case ZEND_FAST_CONCAT:
    case ZEND_IS_IDENTICAL:
    case ZEND_IS_NOT_IDENTICAL:
    case ZEND_IS_EQUAL:
    case ZEND_IS_NOT_EQUAL:
    case ZEND_IS_SMALLER:
    case ZEND_IS_SMALLER_OR_EQUAL:
    case ZEND_BW_OR:
    case ZEND_BW_AND:
    case ZEND_BW_XOR:
    case ZEND_BOOL_XOR:
    case ZEND_BW_NOT:
    case ZEND_POST_INC_OBJ:
    case ZEND_POST_DEC_OBJ:
    case ZEND_POST_INC:
    case ZEND_POST_DEC:
    case ZEND_FETCH_R:
    case ZEND_FETCH_W:
    case ZEND_FETCH_RW:
    case ZEND_FETCH_FUNC_ARG:
    case ZEND_FETCH_UNSET:
    case ZEND_FETCH_IS:
    case ZEND_FETCH_DIM_R:
    case ZEND_FETCH_DIM_W:
    case ZEND_FETCH_DIM_RW:
    case ZEND_FETCH_DIM_IS:
    case ZEND_FETCH_DIM_FUNC_ARG:
    case ZEND_FETCH_DIM_UNSET:
    case ZEND_FETCH_OBJ_R:
    case ZEND_FETCH_OBJ_W:
    case ZEND_FETCH_OBJ_RW:
    case ZEND_FETCH_OBJ_IS:
    case ZEND_FETCH_OBJ_FUNC_ARG:
    case ZEND_FETCH_OBJ_UNSET:
    case ZEND_FETCH_LIST:
    case ZEND_JMPZ_EX:
    case ZEND_JMPNZ_EX:
    case ZEND_ROPE_END:
    case ZEND_FETCH_CLASS:
    case ZEND_RECV:
    case ZEND_RECV_INIT:
    case ZEND_RECV_VARIADIC:
    case ZEND_BOOL:
    case ZEND_CASE:
    case ZEND_FETCH_CONSTANT:
    case ZEND_ADD_ARRAY_ELEMENT:
    case ZEND_INIT_ARRAY:
    case ZEND_CAST:
    case ZEND_FE_RESET_R:
    case ZEND_FE_FETCH_R:
    case ZEND_ISSET_ISEMPTY_VAR:
    case ZEND_ISSET_ISEMPTY_DIM_OBJ:
    case ZEND_ISSET_ISEMPTY_PROP_OBJ:
    case ZEND_BEGIN_SILENCE:
    case ZEND_DECLARE_CLASS:
    case ZEND_DECLARE_INHERITED_CLASS:
    case ZEND_INSTANCEOF:
    case ZEND_DECLARE_LAMBDA_FUNCTION:
    case ZEND_POW:
    case ZEND_ASSIGN_POW:
    case ZEND_STRLEN:
    case ZEND_TYPE_CHECK:
    case ZEND_DEFINED:
    case ZEND_QM_ASSIGN:
    case ZEND_CATCH:
      return true;
    case ZEND_ECHO:
    case ZEND_JMP:
    case ZEND_JMPZ:
    case ZEND_JMPNZ:
    case ZEND_JMPZNZ:
    case ZEND_FREE:
    case ZEND_INIT_STATIC_METHOD_CALL:
    case ZEND_INIT_FCALL_BY_NAME:
    case ZEND_INIT_USER_CALL:
    case ZEND_INIT_NS_FCALL_BY_NAME:
    case ZEND_INIT_FCALL:
    case ZEND_INIT_METHOD_CALL:
    case ZEND_GENERATOR_RETURN:
    case ZEND_THROW:
    case ZEND_SEND_VAL:
    case ZEND_SEND_VAL_EX:
    case ZEND_SEND_VAR:
    case ZEND_SEND_VAR_NO_REF:
    case ZEND_SEND_REF:
    case ZEND_SEND_VAR_EX:
    case ZEND_SEND_UNPACK:
    case ZEND_SEND_ARRAY:
    case ZEND_SEND_USER:
    case ZEND_ROPE_INIT:
    case ZEND_ROPE_ADD:
    case ZEND_UNSET_VAR:
    case ZEND_UNSET_DIM:
    case ZEND_UNSET_OBJ:
    case ZEND_EXIT:
    case ZEND_END_SILENCE:
    case ZEND_JMP_SET:
    case ZEND_COALESCE:
    case ZEND_EXT_STMT:
    case ZEND_EXT_FCALL_BEGIN:
    case ZEND_EXT_FCALL_END:
    case ZEND_DECLARE_INHERITED_CLASS_DELAYED:
    case ZEND_DECLARE_FUNCTION:
    case ZEND_TICKS:
    case ZEND_EXT_NOP:
    case ZEND_NOP:
    case ZEND_ADD_INTERFACE:
    case ZEND_ADD_TRAIT:
    case ZEND_BIND_TRAITS:
    case ZEND_VERIFY_ABSTRACT_CLASS:
    case ZEND_DECLARE_CONST:
    case ZEND_SEPARATE:
    case ZEND_DISCARD_EXCEPTION:
    case ZEND_FAST_CALL:
    case ZEND_BIND_GLOBAL:
    case ZEND_RETURN:
      return false;
    default:
      return (op->result_type & IS_UNUSED) == 0;
  }
}

static void log_operand(FILE *oplog, char index, zend_op_array *ops, znode_op *operand,
                         zend_uchar type)
{
  fprintf(oplog, "[%c|", index);
  switch (type) {
    case IS_CONST: {
      zval *constant = RT_CONSTANT(ops, *operand);

      switch (Z_TYPE_P(constant)) {
        case IS_UNDEF:
          fprintf(oplog, "const <undefined-type>");
          break;
        case IS_NULL:
          fprintf(oplog, "const null");
          break;
        case IS_FALSE:
          fprintf(oplog, "const false");
          break;
        case IS_TRUE:
          fprintf(oplog, "const true");
          break;
        case IS_LONG:
          fprintf(oplog, "const 0x%lx", Z_LVAL_P(constant));
          break;
        case IS_DOUBLE:
          fprintf(oplog, "const %f", Z_DVAL_P(constant));
          break;
        case IS_STRING: {
          uint i, j;
          char buffer[32] = {0};
          const char *str = Z_STRVAL_P(constant);

          for (i = 0, j = 0; i < 31; i++) {
            if (str[i] == '\0')
              break;
            if (str[i] != '\n')
              buffer[j++] = str[i];
          }
          fprintf(oplog, "\"%s\"", buffer);
        } break;
        case IS_ARRAY:
          fprintf(oplog, "const array (zv:"PX")", p2int(constant));
          break;
        case IS_OBJECT:
          fprintf(oplog, "const object? (zv:"PX")", p2int(constant));
          break;
        case IS_RESOURCE:
          fprintf(oplog, "const resource? (zv:"PX")", p2int(constant));
          break;
        case IS_REFERENCE:
          fprintf(oplog, "const reference? (zv:"PX")", p2int(constant));
          break;
        default:
          fprintf(oplog, "const what?? (zv:"PX")", p2int(constant));
          break;
      }
    } break;
    case IS_VAR:
    case IS_TMP_VAR:
      fprintf(oplog, "var #%d", (uint) (EX_VAR_TO_NUM(operand->var) - ops->last_var));
      break;
    case IS_CV:
      fprintf(oplog, "var $%s", ops->vars[EX_VAR_TO_NUM(operand->var)]->val);
      break;
    case IS_UNUSED:
      fprintf(oplog, "-");
      break;
    default:
      fprintf(oplog, "?");
  }
  fprintf(oplog, "]");
}

void dump_operand(application_t *app, char index, zend_op_array *ops, znode_op *operand,
                         zend_uchar type)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  log_operand(oplog, index, ops, operand, type);
}

static void dump_opcode_header(FILE *oplog, uint op_index, zend_op *op)
{
  fprintf(oplog, "\t%04d(L%04d): 0x%02x %s  ", op_index, op->lineno, op->opcode,
          zend_get_opcode_name(op->opcode));
}

void dump_fcall_opcode(application_t *app, zend_op_array *ops, zend_op *op,
                       const char *routine_name)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  dump_opcode_header(oplog, op - ops->opcodes, op);
  if (uses_return_value(op)) {
    log_operand(oplog, 'r', ops, &op->result, op->result_type);
    if (is_db_source_function(NULL, routine_name))
      fprintf(oplog, " <=db= ");
    else if (is_file_source_function(routine_name))
      fprintf(oplog, " <=file= ");
    else if (is_system_source_function(routine_name))
      fprintf(oplog, " <=system= ");
    else
      fprintf(oplog, " = ");
  }
  fprintf(oplog, "%s\n", routine_name);
}

void dump_fcall_arg(application_t *app, zend_op_array *ops, zend_op *op, const char *routine_name)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  dump_opcode_header(oplog, op - ops->opcodes, op);
  log_operand(oplog, 'a', ops, &op->op1, op->op1_type);
  if (op->opcode == ZEND_SEND_ARRAY)
    fprintf(oplog, " -*-> %s\n", routine_name);
  else
    fprintf(oplog, " -%d-> %s\n", op->op2.num, routine_name);
}

void dump_map_assignment(application_t *app, zend_op_array *ops, zend_op *op, zend_op *next_op)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  dump_opcode_header(oplog, op - ops->opcodes, op);
  if (op->op1_type == IS_UNUSED)
    fprintf(oplog, "$this");
  else
    log_operand(oplog, 'm', ops, &op->op1, op->op1_type);
  fprintf(oplog, ".");
  log_operand(oplog, 'k', ops, &op->op2, op->op2_type);
  fprintf(oplog, " = ");
  log_operand(oplog, 'v', ops, &next_op->op1, next_op->op1_type);
  fprintf(oplog, "\n");
}

void dump_foreach_fetch(application_t *app, zend_op_array *ops, zend_op *op, zend_op *next_op)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  dump_opcode_header(oplog, op - ops->opcodes, op);
  fprintf(oplog, " in ");
  log_operand(oplog, 'm', ops, &op->op1, op->op1_type);
  fprintf(oplog, ": ");
  log_operand(oplog, 'k', ops, &next_op->result, next_op->result_type);
  fprintf(oplog, ", ");
  log_operand(oplog, 'v', ops, &op->result, op->result_type);
  fprintf(oplog, "\n");
}

void dump_opcode(application_t *app, zend_op_array *ops, zend_op *op)
{
  zend_op *jump_target = NULL;
  const char *jump_reason = NULL;
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  dump_opcode_header(oplog, op - ops->opcodes, op);

  if (uses_return_value(op)) {
    if (op->result_type != IS_UNUSED) {
      switch (op->opcode) {
        case ZEND_ISSET_ISEMPTY_DIM_OBJ:
        case ZEND_ISSET_ISEMPTY_PROP_OBJ:
        case ZEND_FETCH_DIM_UNSET:
        case ZEND_FETCH_OBJ_UNSET:
        case ZEND_ASSIGN_OBJ:
        case ZEND_ASSIGN_DIM:
        case ZEND_FE_RESET_R:
        case ZEND_FE_FETCH_R:
          log_operand(oplog, 'v', ops, &op->result, op->result_type);
          break;
        default:
          log_operand(oplog, 'r', ops, &op->result, op->result_type);
          break;
      }
      fprintf(oplog, " = ");
    }
    if (op->op1_type == IS_UNUSED && op->op2_type == IS_UNUSED) {
      switch (op->opcode) {
        case ZEND_RECV:
        case ZEND_RECV_INIT:
        case ZEND_RECV_VARIADIC:
          fprintf(oplog, "(arg)");
          break;
        default:
          fprintf(oplog, "(?)");
      }
    }
  }
  if (op->op1_type != IS_UNUSED) {
    switch (op->opcode) {
      case ZEND_FETCH_DIM_R:
      case ZEND_FETCH_DIM_W:
      case ZEND_FETCH_DIM_RW:
      case ZEND_FETCH_DIM_IS:
      case ZEND_FETCH_OBJ_R:
      case ZEND_FETCH_OBJ_W:
      case ZEND_FETCH_OBJ_RW:
      case ZEND_FETCH_OBJ_IS:
      case ZEND_ISSET_ISEMPTY_DIM_OBJ:
      case ZEND_ISSET_ISEMPTY_PROP_OBJ:
      case ZEND_FETCH_DIM_UNSET:
      case ZEND_FETCH_OBJ_UNSET:
      case ZEND_ASSIGN_OBJ:
      case ZEND_ASSIGN_DIM:
      case ZEND_FE_RESET_R:
      case ZEND_FE_FETCH_R:
        log_operand(oplog, 'm', ops, &op->op1, op->op1_type);
        break;
      default:
        log_operand(oplog, '1', ops, &op->op1, op->op1_type);
        break;
    }
  } else {
    switch (op->opcode) {
      case ZEND_JMP:
        jump_target = OP_JMP_ADDR(op, op->op1);
        jump_reason = "(target)";
        break;
      case ZEND_FAST_CALL:
        jump_target = OP_JMP_ADDR(op, op->op1);
        jump_reason = "(?)";
        break;
      case ZEND_FETCH_OBJ_R:
        fprintf(oplog, "$this.");
        break;
    }
  }
  if (op->op1_type != IS_UNUSED && op->op2_type != IS_UNUSED)
    fprintf(oplog, " ? ");
  if (op->op2_type != IS_UNUSED) {
    switch (op->opcode) {
      case ZEND_FETCH_DIM_R:
      case ZEND_FETCH_DIM_W:
      case ZEND_FETCH_DIM_RW:
      case ZEND_FETCH_DIM_IS:
      case ZEND_FETCH_OBJ_R:
      case ZEND_FETCH_OBJ_W:
      case ZEND_FETCH_OBJ_RW:
      case ZEND_FETCH_OBJ_IS:
      case ZEND_ISSET_ISEMPTY_DIM_OBJ:
      case ZEND_ISSET_ISEMPTY_PROP_OBJ:
      case ZEND_FETCH_DIM_UNSET:
      case ZEND_FETCH_OBJ_UNSET:
      case ZEND_ASSIGN_OBJ:
      case ZEND_ASSIGN_DIM:
      case ZEND_FE_RESET_R:
      case ZEND_FE_FETCH_R:
        log_operand(oplog, 'k', ops, &op->op2, op->op2_type);
        break;
      default:
        log_operand(oplog, '2', ops, &op->op2, op->op2_type);
        break;
    }
  } else {
    switch (op->opcode) {
      case ZEND_JMPZ:
      case ZEND_JMPNZ:
      case ZEND_JMPZNZ:
      case ZEND_JMPZ_EX:
      case ZEND_JMPNZ_EX:
        jump_target = OP_JMP_ADDR(op, op->op2);
        jump_reason = "(target)";
        break;
      case ZEND_COALESCE:
        jump_target = OP_JMP_ADDR(op, op->op2);
        jump_reason = "(?)";
        break;
      case ZEND_JMP_SET:
        jump_target = OP_JMP_ADDR(op, op->op2);
        jump_reason = "(?)";
        break;
      case ZEND_FE_RESET_R:
      case ZEND_FE_FETCH_R:
        jump_target = OP_JMP_ADDR(op, op->op2);
        jump_reason = "(if array empty)";
        break;
      case ZEND_NEW:
        jump_target = OP_JMP_ADDR(op, op->op2);
        jump_reason = "(if no constructor)";
        break;
    }
  }
  if (jump_target != NULL) {
    int delta = (int) (jump_target - op);
    fprintf(oplog, " @ %s%d %s", delta > 0 ? "+" : "", delta, jump_reason);
  }
  fprintf(oplog, "\n");
}

static void print_sink(FILE *oplog, const char *sink_details)
{
  fprintf(oplog, "\t      %s\n", sink_details);
}

void identify_sink_operands(application_t *app, zend_op_array *ops, zend_op *op, sink_identifier_t id)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  switch (op->opcode) {
    case ZEND_ADD:
    case ZEND_SUB:
    case ZEND_MUL:
    case ZEND_DIV:
    case ZEND_MOD:
    case ZEND_POW:
    case ZEND_SL:
    case ZEND_SR:
    case ZEND_BW_OR:
    case ZEND_BW_AND:
    case ZEND_BW_XOR:
    case ZEND_BW_NOT:
      print_sink(oplog, "sink(zval:number) {1,2} =d=> {result}");
      break;
    case ZEND_ASSIGN_ADD:
    case ZEND_ASSIGN_SUB:
    case ZEND_ASSIGN_MUL:
    case ZEND_ASSIGN_DIV:
    case ZEND_ASSIGN_MOD:
    case ZEND_ASSIGN_POW:
    case ZEND_ASSIGN_SL:
    case ZEND_ASSIGN_SR:
    case ZEND_ASSIGN_BW_OR:
    case ZEND_ASSIGN_BW_AND:
    case ZEND_ASSIGN_BW_XOR:
      print_sink(oplog, "sink(zval:number) {1,2} =d=> {1,result}");
      break;
    case ZEND_CONCAT:
    case ZEND_FAST_CONCAT:
      print_sink(oplog, "sink(zval:string) {1,2} =d=> {result}");
      break;
    case ZEND_ASSIGN_CONCAT:
      print_sink(oplog, "sink(zval:string) {1,2} =d=> {1,result}");
      break;
    case ZEND_BOOL_XOR:
      print_sink(oplog, "sink(zval:bool) {1,2} =d=> {result}");
      break;
    case ZEND_INSTANCEOF: /* op1 instanceof op2 */
      print_sink(oplog, "sink(zval:bool) {1,2} =d=> {result}");
      break;
    case ZEND_TYPE_CHECK: /* check metadata consistency for op1 */
      print_sink(oplog, "sink(zval:bool) {1} =d=> {result}");
      break;
    case ZEND_DEFINED:
      print_sink(oplog, "sink(zval:bool) {1} =d=> {result}");
      break;
    case ZEND_IS_IDENTICAL:
    case ZEND_IS_NOT_IDENTICAL:
    case ZEND_IS_EQUAL:
    case ZEND_IS_NOT_EQUAL:
    case ZEND_IS_SMALLER:
    case ZEND_IS_SMALLER_OR_EQUAL:
      print_sink(oplog, "sink(zval:bool) {1,2} =d=> {result}");
      break;
    case ZEND_PRE_INC_OBJ:
    case ZEND_PRE_DEC_OBJ:
    case ZEND_POST_INC_OBJ:
    case ZEND_POST_DEC_OBJ:
      print_sink(oplog, "sink(zval:number) {1.2} =d=> {1.2,result}");
      break;
    case ZEND_PRE_INC:
    case ZEND_PRE_DEC:
    case ZEND_POST_INC:
    case ZEND_POST_DEC:
      print_sink(oplog, "sink(zval:number) {1} =d=> {1,result}");
      break;
    case ZEND_ECHO:
      print_sink(oplog, "sink(output) {1} =d=> {output}");
      break;
    case ZEND_FETCH_R:  /* fetch a superglobal */
    case ZEND_FETCH_W:
    case ZEND_FETCH_RW:
    case ZEND_FETCH_IS:
      if (op->op2_type == IS_UNUSED) {
        const char *superglobal_name = Z_STRVAL_P(RT_CONSTANT(ops, op->op1));
        if (superglobal_name != NULL) {
          if (strcmp(superglobal_name, "_SESSION") == 0) {
            print_sink(oplog, "sink(zval) {session} =d=> {result}");
            break;
          } else if (strcmp(superglobal_name, "_REQUEST") == 0 ||
                     strcmp(superglobal_name, "_GET") == 0 ||
                     strcmp(superglobal_name, "_POST") == 0 ||
                     strcmp(superglobal_name, "_COOKIE") == 0 ||
                     strcmp(superglobal_name, "_FILES") == 0) {
            print_sink(oplog, "sink(zval) {user} =d=> {result}");
            break;
          } else if (strcmp(superglobal_name, "_ENV") == 0 ||
                     strcmp(superglobal_name, "_SERVER") == 0) {
            print_sink(oplog, "sink(zval) {env} =d=> {result}");
            break;
          }
        }
        print_sink(oplog, "sink(zval) {1} =d=> {result}");
      } else {
        print_sink(oplog, "sink(zval) {2.1} =d=> {result}");
      }
      break;
    case ZEND_FETCH_UNSET:
      print_sink(oplog, "sink(zval,zval) {1.2} =d=> {result}, {} =d=> {1.2}");
      break;
    case ZEND_FETCH_DIM_R:
    case ZEND_FETCH_DIM_W:
    case ZEND_FETCH_DIM_RW:
    case ZEND_FETCH_DIM_IS:
    case ZEND_FETCH_OBJ_R:
    case ZEND_FETCH_OBJ_W:
    case ZEND_FETCH_OBJ_RW:
    case ZEND_FETCH_OBJ_IS:
      print_sink(oplog, "sink(zval:map) {1.2} =d=> {result}");
      break;
    case ZEND_FETCH_DIM_UNSET:
    case ZEND_FETCH_OBJ_UNSET:
      print_sink(oplog, "sink(zval:map) {1.2} =d=> {result}, {} =d=> {1.2}");
      break;
    case ZEND_FETCH_CONSTANT:
      print_sink(oplog, "sink(zval) {1.2} =d=> {result}");
      break;
    case ZEND_UNSET_VAR:
      if (op->op2_type == IS_UNUSED)
        print_sink(oplog, "sink(zval) {} =d=> {1}");
      else
        print_sink(oplog, "sink(zval:map) {} =d=> {2.1}"); /* op2 must be static */
      break;
    case ZEND_UNSET_DIM:
    case ZEND_UNSET_OBJ:
      print_sink(oplog, "sink(zval:map) {} =d=> {1.2}");
      break;
    case ZEND_ASSIGN:
    case ZEND_ASSIGN_REF:
      print_sink(oplog, "sink(zval) {2} =d=> {1,result}");
      break;
    case ZEND_ASSIGN_OBJ:
    case ZEND_ASSIGN_DIM:
      print_sink(oplog, "sink(zval:map) {value} =d=> {map.key}");
      break;
    case ZEND_JMPZ:
    case ZEND_JMPNZ:
    case ZEND_JMPZNZ:
    case ZEND_JMPZ_EX:
    case ZEND_JMPNZ_EX:
      print_sink(oplog, "sink(branch) {1} =l=> {opline}");
      break;
    case ZEND_ROPE_INIT:
    case ZEND_ROPE_ADD:
      print_sink(oplog, "sink(rope) {2} =d=> {1}");
      break;
    case ZEND_ROPE_END:    /* (may convert op2 to string) */
      print_sink(oplog, "sink(rope) {1,2} =d=> {result}");
      break;
    case ZEND_ADD_ARRAY_ELEMENT: /* insert or append */
      if (op->op2_type == IS_UNUSED)
        print_sink(oplog, "sink(zval:map) {1} =d=> {result[size]}");
      else
        print_sink(oplog, "sink(zval:map) {1} =d=> {result[2]}");
      break;
    case ZEND_INIT_METHOD_CALL:
      print_sink(oplog, "sink(edge) {1.2} =d=> {fcall-stack}");
      break;
    case ZEND_INIT_STATIC_METHOD_CALL:
      print_sink(oplog, "sink(edge) {1.2} =d=> {fcall-stack}");
      break;
    case ZEND_INIT_FCALL:
    case ZEND_INIT_FCALL_BY_NAME:
    case ZEND_INIT_USER_CALL:
    case ZEND_INIT_NS_FCALL_BY_NAME:
      print_sink(oplog, "sink(edge) {2} =d=> {fcall-stack}");
      break;
    case ZEND_NEW:
      print_sink(oplog, "sink(edge) {1} =d=> {fcall-stack} (or skip via 2 => opline if no ctor)");
      break;
    case ZEND_DO_FCALL:
    case ZEND_DO_ICALL:
      if (is_db_sink_function(NULL, id.call_target))
        print_sink(oplog, "sink(edge) {fcall-stack} =d=> {db,opline,result}");
      else if (is_file_sink_function(id.call_target))
        print_sink(oplog, "sink(edge) {fcall-stack} =d=> {file,opline,result}");
      else if (is_system_sink_function(id.call_target))
        print_sink(oplog, "sink(edge) {fcall-stack} =d=> {system,opline,result}");
      else
        print_sink(oplog, "sink(edge) {fcall-stack} =d=> {opline,result}");
      break;
    case ZEND_RETURN:
    case ZEND_RETURN_BY_REF:
      print_sink(oplog, "sink(return) {1} =d=> {return}");
      break;
    case ZEND_INCLUDE_OR_EVAL:
      if (op->extended_value == ZEND_EVAL)
        print_sink(oplog, "sink(edge) {1} =d=> {code,opline}");
      else
        print_sink(oplog, "sink(edge) {1} =d=> {opline}, {file} =d=> {code}");
      break;
    case ZEND_CLONE:
      print_sink(oplog, "sink(edge) {1} =d=> {opline,result}");
      break;
    case ZEND_THROW:
      print_sink(oplog, "sink(edge) {1} =d=> {fast-ret,thrown}");
      break;
    case ZEND_HANDLE_EXCEPTION: /* makes fastcall to finally blocks */
      print_sink(oplog, "sink(edge) {thrown} =i=> {opline}");
      break;
    case ZEND_DISCARD_EXCEPTION:
      print_sink(oplog, "sink(edge) {thrown} =d=> {thrown}");
      break;
    case ZEND_FAST_CALL: /* call finally via no-arg fastcall (when not handling exception) */
      print_sink(oplog, "sink(edge) {thrown} =d=> {fast-ret,opline}");
      break;
    case ZEND_FAST_RET: /* return from fastcall */
      print_sink(oplog, "sink(edge) {fast-ret} =i=> {opline}");
      break;
    case ZEND_SEND_VAL:
    case ZEND_SEND_VAL_EX:
    case ZEND_SEND_VAR:
    case ZEND_SEND_VAR_NO_REF:
    case ZEND_SEND_REF:
    case ZEND_SEND_USER:
      print_sink(oplog, "sink(zval) {1} =d=> {fcall-stack(2 is arg#)}");
      break;
    case ZEND_SEND_ARRAY:
      print_sink(oplog, "sink(zval) {1} =d=> {fcall-stack(all args)}");
      break;
    case ZEND_RECV:
    case ZEND_RECV_VARIADIC:
      print_sink(oplog, "sink(zval) {fcall-stack(arg)} =i=> {result}");
      break;
    case ZEND_BOOL:
    case ZEND_BOOL_NOT:
      print_sink(oplog, "sink(zval) {1} =d=> {result}");
      break;
    case ZEND_CASE: /* execute case if 1 == 2 (via fast_equal_function) */
      print_sink(oplog, "sink(zval:bool) {1,2} =l=> {result}");
      break;
    case ZEND_CAST:
      print_sink(oplog, "sink(zval) {1,ext} =d=> {result}"); /* ext specifies cast dest type */
      break;
    case ZEND_FE_RESET_R: /* starts an iterator */
      print_sink(oplog, "sink(zval:map) {1} =d=> {result} (or skips via 2 => opline if 1 is empty)");
      break;
    case ZEND_FE_FETCH_R: /* advances an iterator */
      print_sink(oplog, "sink(zval:map) {1} =d=> {result(key), next_op.result(value)} "
                       "(or terminates via 2 => opline}");
      break;
    case ZEND_ISSET_ISEMPTY_VAR:
      if (op->op2_type == IS_UNUSED)
        print_sink(oplog, "sink(zval:bool) {1} =d=> {result}"); // is this a superglobal source?
      else
        print_sink(oplog, "sink(zval:bool) {2.1} =d=> {result}"); /* op2 must be static */
      break;
    case ZEND_ISSET_ISEMPTY_DIM_OBJ:
    case ZEND_ISSET_ISEMPTY_PROP_OBJ:
      print_sink(oplog, "sink(zval:bool) {1.2} =d=> {result}");
      break;
    case ZEND_EXIT:
      if (op->op1_type == IS_UNUSED)
        print_sink(oplog, "sink(edge) {} =d=> {opline}");
      else
        print_sink(oplog, "sink(edge) {} =d=> {opline}, {1} =d=> {exit-code}");
      break;
    case ZEND_BEGIN_SILENCE:
    case ZEND_END_SILENCE:
      print_sink(oplog, "sink(internal) {} =d=> {error-reporting}");
      break;
    case ZEND_TICKS:
      print_sink(oplog, "sink(internal) {ext} =d=> {timer}");
      break;
    case ZEND_JMP_SET:
      print_sink(oplog, "sink(branch) {1} =l=> {result,opline}"); /* NOP if op1 is false */
      break;
    case ZEND_COALESCE:
      print_sink(oplog, "sink(branch) {1} =l=> {result,opline}"); /* NOP if op1 is NULL */
      break;
    case ZEND_QM_ASSIGN:
      print_sink(oplog, "sink(zval?) {1} =?=> {result}");
      break;
    case ZEND_DECLARE_CLASS:
    case ZEND_DECLARE_INHERITED_CLASS:
      print_sink(oplog, "sink(code) {1} =i=> {code}");
      break;
    case ZEND_DECLARE_INHERITED_CLASS_DELAYED: /* bind op1 only if op2 is unbound */
      print_sink(oplog, "sink(code) {1,2} =i=> {code}");
      break;
    case ZEND_DECLARE_FUNCTION:
      print_sink(oplog, "sink(code) {compiler} =i=> {code}");
      break;
    case ZEND_ADD_INTERFACE:
    case ZEND_ADD_TRAIT: /* add interface/trait op2 (with zv+1) to class op1 */
      print_sink(oplog, "sink(code) {1(.[2,2.zv+1])} =i=> {code}");
      break;
    case ZEND_BIND_TRAITS:
      print_sink(oplog, "sink(code) {1} =i=> {code}"); /* bind pending traits of op1 */
      break;
    case ZEND_DECLARE_CONST:
      print_sink(oplog, "sink(zval) {1.2} =d=> {global}");
      break;
    case ZEND_BIND_GLOBAL: /* binds value in op2 to global named op1 */
      print_sink(oplog, "sink(global) {1,2} =d=> {global}");
      break;
    case ZEND_STRLEN:
      print_sink(oplog, "sink(zval:number) {1} =d=> {result}");
      break;

/* ======== SINK TODO ======= *

  ZEND_SEPARATE  --  {1} =d=> {?} (unbinds op1 somehow)

  ZEND_FETCH_FUNC_ARG?
  ZEND_FETCH_DIM_FUNC_ARG?
  ZEND_FETCH_OBJ_FUNC_ARG?

  ZEND_FETCH_LIST

  ZEND_RETURN_BY_REF
  ZEND_GENERATOR_RETURN

  ZEND_FETCH_CLASS

  ZEND_SEND_UNPACK

  ZEND_EXT_STMT
  ZEND_EXT_FCALL_BEGIN
  ZEND_EXT_FCALL_END

  ZEND_YIELD

  ZEND_DECLARE_LAMBDA_FUNCTION -- tricky scope rules
*/

/* ======= SINK NOP ======= *

  ZEND_NOP
  ZEND_EXT_NOP
  ZEND_JMP
  ZEND_FREE
  ZEND_DO_FCALL
  ZEND_VERIFY_ABSTRACT_CLASS
  ZEND_USER_OPCODE   -- customizable opcode handlers...!
*/

  }
}

#define DB_STMT_TYPE "mysqli_stmt"
#define DB_STMT_PREFIX COMPILED_ROUTINE_DEFAULT_SCOPE":mysqli_stmt_"
#define DB_STMT_PREFIX_LEN (strlen(DB_STMT_PREFIX))

static bool is_db_stmt_function_base(const char *type, const char *name)
{
  if (type == NULL) {
    if (strncmp(name, DB_STMT_PREFIX, DB_STMT_PREFIX_LEN) != 0)
      return false;
  } else {
    if (strncmp(name, type, strlen(type)) != 0)
      return false;
  }
  return true;
}

#define DB_SQL_TYPE "mysqli"
#define DB_SQL_PREFIX COMPILED_ROUTINE_DEFAULT_SCOPE":mysqli_"
#define DB_SQL_PREFIX_LEN (strlen(DB_SQL_PREFIX))

static bool is_db_sql_function_base(const char *type, const char *name)
{
  if (type == NULL && is_db_stmt_function_base(type, name)) // FIXME: distinguishing stmt functions
    return false;

  if (type == NULL) {
    if (strncmp(name, DB_SQL_PREFIX, DB_SQL_PREFIX_LEN) != 0)
      return false;
  } else {
    if (strncmp(name, type, strlen(type)) != 0)
      return false;
  }

  return true;
}

static bool is_db_sql_source_function(const char *name)
{ /* separate cases to allow for a possible enum mapping later on */
  if (strcmp(name, "connect") == 0) {
    return true;
  } else if (strcmp(name, "fetch_field") == 0) {
    return true;
  } else if (strcmp(name, "fetch_fields") == 0) {
    return true;
  } else if (strcmp(name, "fetch_field_direct") == 0) {
    return true;
  } else if (strcmp(name, "fetch_lengths") == 0) {
    return true;
  } else if (strcmp(name, "fetch_all") == 0) {
    return true;
  } else if (strcmp(name, "fetch_array") == 0) {
    return true;
  } else if (strcmp(name, "fetch_assoc") == 0) {
    return true;
  } else if (strcmp(name, "fetch_object") == 0) {
    return true;
  } else if (strcmp(name, "fetch_row") == 0) {
    return true;
  } else if (strcmp(name, "field_count") == 0) {
    return true;
  } else if (strcmp(name, "field_seek") == 0) {
    return true;
  } else if (strcmp(name, "field_tell") == 0) {
    return true;
  } else if (strcmp(name, "insert_id") == 0) {
    return true;
  } else if (strcmp(name, "next_result") == 0) {
    return true;
  } else if (strcmp(name, "num_fields") == 0) {
    return true;
  } else if (strcmp(name, "num_rows") == 0) {
    return true;
  } else if (strcmp(name, "reap_async_query") == 0) {
    return true;
  }
  return false;
}

static bool is_db_stmt_source_function(const char *name)
{
  if (strcmp(name, "bind_result") == 0) {
    return true;
  } else if (strcmp(name, "data_seek") == 0) {
    return true;
  } else if (strcmp(name, "fetch") == 0) {
    return true;
  } else if (strcmp(name, "field_count") == 0) {
    return true;
  } else if (strcmp(name, "get_result") == 0) {
    return true;
  } else if (strcmp(name, "insert_id") == 0) {
    return true;
  } else if (strcmp(name, "more_results") == 0) {
    return true;
  } else if (strcmp(name, "next_result") == 0) {
    return true;
  } else if (strcmp(name, "num_rows") == 0) {
    return true;
  } else if (strcmp(name, "param_count") == 0) {
    return true;
  } else if (strcmp(name, "result_metadata") == 0) {
    return true;
  }
  return false;
}

bool is_db_source_function(const char *type, const char *name)
{
  return (is_db_sql_function_base(type, name) &&
          is_db_sql_source_function(name + (type == NULL ? DB_SQL_PREFIX_LEN : strlen(type)))) ||
         (is_db_stmt_function_base(type, name) &&
          is_db_stmt_source_function(name + (type == NULL ? DB_STMT_PREFIX_LEN : strlen(type))));
}

static bool is_db_sql_sink_function(const char *name)
{ /* separate cases to allow for a possible enum mapping later on */
  if (strcmp(name, "multi_query") == 0) {
    return true;
  } else if (strcmp(name, "prepare") == 0) {
    return true;
  } else if (strcmp(name, "query") == 0) { /* not a source: only the fetch functions are sources */
    return true;
  } else if (strcmp(name, "multi_query") == 0) {
    return true;
  } else if (strcmp(name, "select_db") == 0) {
    return true;
  }
  return false;
}

static bool is_db_stmt_sink_function(const char *name)
{
  if (strcmp(name, "attr_get") == 0) {
    return true;
  } else if (strcmp(name, "attr_set") == 0) {
    return true;
  } else if (strcmp(name, "bind_param") == 0) {
    return true;
  } else if (strcmp(name, "prepare") == 0) {
    return true;
  } else if (strcmp(name, "reset") == 0) {
    return true;
  } else if (strcmp(name, "send_long_data") == 0) {
    return true;
  }
  return false;
}

bool is_db_sink_function(const char *type, const char *name)
{
  return (is_db_sql_function_base(type, name) &&
          is_db_sql_sink_function(name + (type == NULL ? DB_SQL_PREFIX_LEN : strlen(type)))) ||
         (is_db_stmt_function_base(type, name) &&
          is_db_stmt_sink_function(name + (type == NULL ? DB_STMT_PREFIX_LEN : strlen(type))));
}

bool is_file_source_function(const char *name)
{
  if (strcmp(name, "dir") == 0) {
    return true;
  } else if (strcmp(name, "getcwd") == 0) {
    return true;
  } else if (strcmp(name, "opendir") == 0) {
    return true;
  } else if (strcmp(name, "readdir") == 0) {
    return true;
  } else if (strcmp(name, "scandir") == 0) {
    return true;
  } else if (strcmp(name, "basename") == 0) {
    return true;
  } else if (strcmp(name, "dirname") == 0) {
    return true;
  } else if (strcmp(name, "disk_free_space") == 0) {
    return true;
  } else if (strcmp(name, "disk_total_space") == 0) {
    return true;
  } else if (strcmp(name, "diskfreespace") == 0) {
    return true;
  } else if (strcmp(name, "feof") == 0) {
    return true;
  } else if (strcmp(name, "fgetc") == 0) {
    return true;
  } else if (strcmp(name, "fgetcsv") == 0) {
    return true;
  } else if (strcmp(name, "fgets") == 0) {
    return true;
  } else if (strcmp(name, "fgetss") == 0) {
    return true;
  } else if (strcmp(name, "file_exists") == 0) {
    return true;
  } else if (strcmp(name, "file_get_contents") == 0) {
    return true;
  } else if (strcmp(name, "file") == 0) {
    return true;
  } else if (strcmp(name, "fileatime") == 0) {
    return true;
  } else if (strcmp(name, "filectime") == 0) {
    return true;
  } else if (strcmp(name, "filegroup") == 0) {
    return true;
  } else if (strcmp(name, "fileinode") == 0) {
    return true;
  } else if (strcmp(name, "filemtime") == 0) {
    return true;
  } else if (strcmp(name, "fileowner") == 0) {
    return true;
  } else if (strcmp(name, "fileperms") == 0) {
    return true;
  } else if (strcmp(name, "filesize") == 0) {
    return true;
  } else if (strcmp(name, "filetype") == 0) {
    return true;
  } else if (strcmp(name, "fnmatch") == 0) {
    return true;
  } else if (strcmp(name, "fpassthru") == 0) {
    return true;
  } else if (strcmp(name, "fread") == 0) {
    return true;
  } else if (strcmp(name, "fscanf") == 0) {
    return true;
  } else if (strcmp(name, "fstat") == 0) {
    return true;
  } else if (strcmp(name, "ftell") == 0) {
    return true;
  } else if (strcmp(name, "glob") == 0) {
    return true;
  } else if (strcmp(name, "is_dir") == 0) {
    return true;
  } else if (strcmp(name, "is_executable") == 0) {
    return true;
  } else if (strcmp(name, "is_file") == 0) {
    return true;
  } else if (strcmp(name, "is_link") == 0) {
    return true;
  } else if (strcmp(name, "is_readable") == 0) {
    return true;
  } else if (strcmp(name, "is_uploaded_file") == 0) {
    return true;
  } else if (strcmp(name, "is_writable") == 0) {
    return true;
  } else if (strcmp(name, "is_writeable") == 0) {
    return true;
  } else if (strcmp(name, "fclose") == 0) {
    return true;
  } else if (strcmp(name, "fseek") == 0) {
    return true;
  } else if (strcmp(name, "rewind") == 0) {
    return true;
  }
  return false;
}

bool is_file_sink_function(const char *name)
{
  if (strcmp(name, "chdir") == 0) {
    return true;
  } else if (strcmp(name, "chroot") == 0) {
    return true;
  } else if (strcmp(name, "closedir") == 0) {
    return true;
  } else if (strcmp(name, "rewinddir") == 0) {
    return true;
  } else if (strcmp(name, "chgrp") == 0) {
    return true;
  } else if (strcmp(name, "chmod") == 0) {
    return true;
  } else if (strcmp(name, "chown") == 0) {
    return true;
  } else if (strcmp(name, "copy") == 0) {
    return true;
  } else if (strcmp(name, "delete") == 0) {
    return true;
  } else if (strcmp(name, "fflush") == 0) {
    return true;
  } else if (strcmp(name, "file_put_contents") == 0) {
    return true;
  } else if (strcmp(name, "flock") == 0) {
    return true;
  } else if (strcmp(name, "fopen") == 0) { /* may open newtork connection to anywhere */
    return true;
  } else if (strcmp(name, "fputcsv") == 0) {
    return true;
  } else if (strcmp(name, "fputs") == 0) {
    return true;
  } else if (strcmp(name, "ftruncate") == 0) {
    return true;
  } else if (strcmp(name, "fwrite") == 0) {
    return true;
  } else if (strcmp(name, "lchgrp") == 0) {
    return true;
  } else if (strcmp(name, "lchown") == 0) {
    return true;
  } else if (strcmp(name, "mkdir") == 0) {
    return true;
  } else if (strcmp(name, "move_uploaded_file") == 0) {
    return true;
  } else if (strcmp(name, "rename") == 0) {
    return true;
  } else if (strcmp(name, "rmdir") == 0) {
    return true;
  } else if (strcmp(name, "set_file_buffer") == 0) {
    return true;
  } else if (strcmp(name, "link") == 0) {
    return true;
  } else if (strcmp(name, "symlink") == 0) {
    return true;
  } else if (strcmp(name, "unlink") == 0) {
    return true;
  } else if (strcmp(name, "touch") == 0) {
    return true;
  } else if (strcmp(name, "tmpfile") == 0) {
    return true;
  }
  return false;
}

bool is_system_source_function(const char *name)
{
  if (strcmp(name, "shmop_read") == 0) {
    return true;
  } else if (strcmp(name, "shmop_size") == 0) {
    return true;
  } else if (strcmp(name, "expect_expectl") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_alarm") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_errno") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_get_last_err") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_getpriority") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_strerror") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wexitstatus") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wifexited") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wifsignaled") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wifstopped") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wstopsig") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wtermsig") == 0) {
    return true;
  } else if (strcmp(name, "linkinfo") == 0) {
    return true;
  } else if (strcmp(name, "lstat") == 0) {
    return true;
  } else if (strcmp(name, "parse_ini_file") == 0) {
    return true;
  } else if (strcmp(name, "parse_ini_string") == 0) {
    return true;
  } else if (strcmp(name, "pathinfo") == 0) {
    return true;
  } else if (strcmp(name, "readfile") == 0) {
    return true;
  } else if (strcmp(name, "readlink") == 0) {
    return true;
  } else if (strcmp(name, "realpath_cache_get") == 0) {
    return true;
  } else if (strcmp(name, "realpath_cache_size") == 0) {
    return true;
  } else if (strcmp(name, "realpath") == 0) {
    return true;
  } else if (strcmp(name, "stat") == 0) {
  //} else if (strcmp(name, "") == 0) {
  }
  return false;
}

bool is_system_sink_function(const char *name)
{
  if (strcmp(name, "shmop_close") == 0) {
    return true;
  } else if (strcmp(name, "shmop_delete") == 0) {
    return true;
  } else if (strcmp(name, "shmop_open") == 0) {
    return true;
  } else if (strcmp(name, "shmop_write") == 0) {
    return true;
  } else if (strcmp(name, "expect_popen") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_exec") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_fork") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_setpriority") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_signal_dispatch") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_signal") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_sigprocmask") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_sigtimedwait") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_sigwaitinfo") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_wait") == 0) {
    return true;
  } else if (strcmp(name, "pcntl_waitpid") == 0) {
    return true;
  } else if (strcmp(name, "clearstatcache") == 0) {
    return true;
  } else if (strcmp(name, "pclose") == 0) {
    return true;
  } else if (strcmp(name, "popen") == 0) {
    return true;
  } else if (strcmp(name, "tempnam") == 0) {
    return true;
  } else if (strcmp(name, "umask") == 0) {
    return true;
  }
  return false;
}

/**************************************************************************************
 Static Dataflow Analysis
 **************************************************************************************/

typedef struct _dataflow_file_t {
  char *path;
  bool analyzed;
  zend_op_array *zops;
  struct _dataflow_file_t *next;
} dataflow_file_t;

typedef struct _dataflow_call_by_name_t {
  const char *name;
} dataflow_call_by_name_t;

typedef enum _dataflow_call_flags_t {
  DATAFLOW_CALL_FLAG_BY_NAME = 0x01,
  DATAFLOW_CALL_FLAG_BUILTIN = 0x02,
} dataflow_call_flags_t;

typedef struct _dataflow_call_site_t {
  uint opcode_index;
  uint target_hash;
  dataflow_call_flags_t flags;
  scarray_t *args; // dataflow_sink_t *
  union {
    dataflow_call_by_name_t call_by_name;
  };
} dataflow_call_site_t;

typedef struct _dataflow_routine_t {
  application_t *app;
  uint routine_hash;
  scarray_t opcodes; // dataflow_opcode_t *
  scarray_t sources; // dataflow_source_t *
  scarray_t sinks; // dataflow_sink_t *
  scarray_t *parameters; // dataflow_source_t *
  scarray_t *call_sites; // dataflow_call_site_t *
} dataflow_routine_t;

typedef struct _dataflow_graph_t {
  dataflow_routine_t *top_routine;
  sctable_t routine_table;
  scarray_t routine_list; // N.B.: the routine_list does not contain include/require routines
} dataflow_graph_t;

typedef struct _dataflow_live_variable_t {
  bool is_temp;
  union {
    uint temp_id;
    const char *var_name;
  };
  dataflow_operand_id_t src;
  cfg_opcode_id_t global_id;
} dataflow_live_variable_t;

#define IS_LIVE_GLOBAL(var) ((var) != NULL && (var)->global_id.routine_hash != 0U)

typedef struct _dataflow_link_pass_t {
  bool complete;
  dataflow_routine_t *droutine;
  uint start_index;
  dataflow_routine_t *resume_droutine;
  uint resume_index;
  scarray_t *live_variables; // dataflow_live_variable_t *
} dataflow_link_pass_t;

static dataflow_file_t *dataflow_file_list = NULL;
static dataflow_graph_t *dg = NULL;
static dataflow_routine_t *assembling_routine = NULL;
static scarray_t dataflow_link_worklist; // dataflow_link_pass_t *
static scarray_t fcall_stack; // dataflow_call_site_t *
static bool analyzing_include = false;
static bool dataflow_linking_complete, source_propagation_complete, function_propagation_complete;

void init_dataflow_analysis()
{
  scarray_init(&fcall_stack);
}

static int analyze_file_dataflow(zend_file_handle *in, dataflow_file_t *out)
{
  int retval = FAILURE;

  SPOT("analyze_file_dataflow() for file %s\n", in->filename);

  zend_try {
    out->zops = zend_compile_file(in, ZEND_INCLUDE TSRMLS_CC);
    zend_destroy_file_handle(in TSRMLS_CC);
    if (out->zops != NULL)
      retval = SUCCESS;
  } zend_end_try();

  return retval;
}

static inline dataflow_operand_t *get_dataflow_operand(dataflow_opcode_t *dop,
                                                       dataflow_operand_index_t index)
{
  switch (index) {
    case DATAFLOW_OPERAND_RESULT:
      return &dop->result;
    case DATAFLOW_OPERAND_1:
      return &dop->op1;
    case DATAFLOW_OPERAND_2:
      return &dop->op2;
    default:
      return NULL;
  }
}

static dataflow_live_variable_t *lookup_live_named_variable(scarray_t *live_variables, uint *index,
                                                            const char *var_name)
{
  uint i;
  dataflow_live_variable_t *var;

  for (i = *index; i < live_variables->size; i++) {
    var = (dataflow_live_variable_t *) live_variables->data[i];
    if (!var->is_temp && strcmp(var->var_name, var_name) == 0) {
      *index = i + 1;
      return var;
    }
  }
  return NULL;
}

static dataflow_live_variable_t *lookup_live_temp_variable(scarray_t *live_variables, uint *index,
                                                           uint temp_id)
{
  uint i;
  dataflow_live_variable_t *var;

  for (i = *index; i < live_variables->size; i++) {
    var = (dataflow_live_variable_t *) live_variables->data[i];
    if (var->is_temp && var->temp_id == temp_id) {
      *index = i + 1;
      return var;
    }
  }
  return NULL;
}

static dataflow_live_variable_t *lookup_live_variable(scarray_t *live_variables, uint *index,
                                                      dataflow_operand_t *doperand)
{
  dataflow_live_variable_t *var = NULL;

  switch (doperand->value.type) {
    case DATAFLOW_VALUE_TYPE_NONE:
    case DATAFLOW_VALUE_TYPE_CONST:
    case DATAFLOW_VALUE_TYPE_JMP: // it's a sink, though
    case DATAFLOW_VALUE_TYPE_INCLUDE:
    case DATAFLOW_VALUE_TYPE_FCALL:
      return NULL; // nothing to link
    case DATAFLOW_VALUE_TYPE_VAR:
      var = lookup_live_named_variable(live_variables, index, doperand->value.var_name);
      break;
    case DATAFLOW_VALUE_TYPE_THIS:
      var = lookup_live_named_variable(live_variables, index, "this");
      break;
    case DATAFLOW_VALUE_TYPE_TEMP:
      var = lookup_live_temp_variable(live_variables, index, doperand->value.temp_id);
      break;
  }

  return var;
}

static inline bool is_same_opcode_id(cfg_opcode_id_t *first, cfg_opcode_id_t *second)
{
  return first->routine_hash == second->routine_hash && first->op_index == second->op_index;
}

static inline bool is_same_operand_id(dataflow_operand_id_t *first, dataflow_operand_id_t *second)
{
  return is_same_opcode_id(&first->opcode_id, &second->opcode_id) &&
         first->operand_index == second->operand_index;
}

static void get_var_name(dataflow_operand_t *doperand, char *var_name, uint max)
{
  switch (doperand->value.type) {
    case DATAFLOW_VALUE_TYPE_VAR:
      strncpy(var_name, doperand->value.var_name, max);
      break;
    case DATAFLOW_VALUE_TYPE_THIS:
      strncpy(var_name, DATAFLOW_VAR_NAME_THIS, max);
      break;
    case DATAFLOW_VALUE_TYPE_TEMP:
      snprintf(var_name, max, "#%d", doperand->value.temp_id);
      break;
    default:
      snprintf(var_name, max, "%d?", doperand->value.type);
      break;
  }
}

static inline char get_operand_index_code(dataflow_operand_index_t index)
{
  switch (index) {
    case DATAFLOW_OPERAND_RESULT:
      return 'r';
    case DATAFLOW_OPERAND_1:
      return '1';
    case DATAFLOW_OPERAND_2:
      return '2';
    default:
      return '?';
  }
}

static void assign_destination(FILE *oplog, scarray_t *live_variables, dataflow_opcode_t *dop,
                               dataflow_operand_t *dst, dataflow_operand_index_t dst_index)
{
  char var_name[256];
  uint lookup_index = 0;
  dataflow_live_variable_t *dst_var;
  bool existing_dst_var = false, found_any = false;
  cfg_opcode_id_t global_id = {0};

  get_var_name(dst, var_name, 256);
  while ((dst_var = lookup_live_variable(live_variables, &lookup_index, dst)) != NULL) {
    if (is_same_opcode_id(&dst_var->src.opcode_id, &dop->id)) {
      existing_dst_var = true;
    } else {
      dataflow_live_variable_t *clobbered = scarray_remove(live_variables,
                                                           lookup_index - 1 /* kind of hackish */);

      fprintf(oplog, "Clobber %svariable '%s' at 0x%x:%d.%c with 0x%x:%d.%c\n",
              IS_LIVE_GLOBAL(clobbered) ? "global " : "", var_name,
              dst_var->src.opcode_id.routine_hash, dst_var->src.opcode_id.op_index,
              get_operand_index_code(dst_var->src.operand_index),
              dop->id.routine_hash, dop->id.op_index, get_operand_index_code(dst_index));

      found_any = true;
      if (IS_LIVE_GLOBAL(clobbered))
        global_id = clobbered->global_id;
      PROCESS_FREE(clobbered);
    }
  }

  if (existing_dst_var)
    return;

  if (!found_any) {
    fprintf(oplog, "Nothing clobbered for var '%s' (%d.%c)\n", var_name,
            dop->id.op_index, get_operand_index_code(dst_index));
  }

  dst_var = PROCESS_NEW(dataflow_live_variable_t); // mem: what scope for dataflow?
  memset(dst_var, 0, sizeof(dataflow_live_variable_t));
  dst_var->is_temp = (dst->value.type == DATAFLOW_VALUE_TYPE_TEMP);
  if (dst_var->is_temp) {
    dst_var->temp_id = dst->value.temp_id;
  } else {
    if (dst->value.type == DATAFLOW_VALUE_TYPE_THIS)
      dst_var->var_name = DATAFLOW_VAR_NAME_THIS;
    else {
      if (dst->value.var_name == NULL)
        dst_var->var_name = DATAFLOW_VAR_NAME_MISSING;
      else
        dst_var->var_name = dst->value.var_name;
    }
  }
  dst_var->src.opcode_id = dop->id;
  dst_var->src.operand_index = dst_index;
  dst_var->global_id = global_id;
  scarray_append(live_variables, dst_var);
}

static bool add_predecessor(dataflow_predecessor_t **predecessor, cfg_opcode_id_t *op_id,
                            dataflow_operand_index_t op_index)
{
  bool existing_predecessor = false;
  dataflow_predecessor_t *p = *predecessor;

  while (p != NULL) {
    if (is_same_opcode_id(&p->operand_id.opcode_id, op_id) &&
        p->operand_id.operand_index == op_index) {
      existing_predecessor = true;
      break;
    }
    p = p->next;
  }
  if (!existing_predecessor) {
    dataflow_predecessor_t *p = PROCESS_NEW(dataflow_predecessor_t);
    p->operand_id.opcode_id = *op_id;
    p->operand_id.operand_index = op_index;
    p->next = *predecessor;
    *predecessor = p;
    return true;
  }
  return false;
}

static void link_operand(FILE *oplog, scarray_t *live_variables, dataflow_opcode_t *dop,
                         dataflow_operand_index_t src_index, dataflow_operand_index_t dst_index,
                         bool commit)
{
  char var_name[256];
  uint lookup_index = 0;
  dataflow_operand_t *src = get_dataflow_operand(dop, src_index);
  dataflow_operand_t *dst = get_dataflow_operand(dop, dst_index);

  /* Global src flows from all global bind points in scope. */
  if (src_index != DATAFLOW_OPERAND_SOURCE) {
    dataflow_live_variable_t *src_var;

    get_var_name(src, var_name, 256);
    while ((src_var = lookup_live_variable(live_variables, &lookup_index, src)) != NULL) {
      if (IS_LIVE_GLOBAL(src_var)) {
        dataflow_operand_t *global_dst = (dst == NULL ? src : dst);

        fprintf(oplog, "Attempting to link global source %s to 0x%x:%d\n", var_name,
                dop->id.routine_hash, dop->id.op_index);

        if (add_predecessor(&global_dst->predecessor, &src_var->global_id,
                            DATAFLOW_OPERAND_SOURCE)) {
          fprintf(oplog, "Link global variable '%s': "
                  "0x%x:%d.source -%d-> 0x%x:%d.%c\n", var_name,
                  src_var->global_id.routine_hash, src_var->global_id.op_index, src_index,
                  dop->id.routine_hash, dop->id.op_index,
                  get_operand_index_code(dst_index));
        }
      }
    }
  }

  /* Global dst flows to all global bind points in scope. */
  lookup_index = 0;
  if (dst_index != DATAFLOW_OPERAND_SINK) {
    dataflow_live_variable_t *dst_var;

    get_var_name(dst, var_name, 256);
    while ((dst_var = lookup_live_variable(live_variables, &lookup_index, dst)) != NULL) {
      if (IS_LIVE_GLOBAL(dst_var)) {
        dataflow_routine_t *sink_routine = sctable_lookup(&dg->routine_table,
                                                          dst_var->global_id.routine_hash);
        dataflow_opcode_t *sink_op = (dataflow_opcode_t *) sink_routine->opcodes.data[dst_var->global_id.op_index];

        fprintf(oplog, "Attempting to link 0x%x:%d to global sink of variable %s\n",
                dop->id.routine_hash, dop->id.op_index, var_name);

        if (add_predecessor(&sink_op->sink.predecessor, &dst_var->src.opcode_id,
                            dst_var->src.operand_index)) {
          fprintf(oplog, "Link to global variable '%s': 0x%x:%d.%c -%d-> 0x%x:%d.sink\n",
                  var_name, dop->id.routine_hash, dop->id.op_index,
                  get_operand_index_code(src_index),
                  dst_index, sink_op->id.routine_hash, sink_op->id.op_index);
        }
      }
    }
  }
  lookup_index = 0;

  //fprintf(oplog, "   [ link_operand %d.(%d->%d)) ]\n",
  //        dop->id.op_index, src_index, dst_index);

  if (src_index == DATAFLOW_OPERAND_SOURCE) {
    dataflow_operand_t *dst = get_dataflow_operand(dop, dst_index);

    if (add_predecessor(&dst->predecessor, &dop->id, src_index)) {
      fprintf(oplog, "Link source type %d into 0x%x:%d.%d\n", dop->source.type,
              dop->id.routine_hash, dop->id.op_index, dst_index);
    }

    if (commit)
      assign_destination(oplog, live_variables, dop, dst, dst_index);
  } else if (dst_index == DATAFLOW_OPERAND_SINK) {
    dataflow_operand_t *src = get_dataflow_operand(dop, src_index);
    dataflow_live_variable_t *src_var;

    switch (src->value.type) {
      case DATAFLOW_VALUE_TYPE_VAR:
        if (dop->sink.type == SINK_TYPE_GLOBAL) { /* create an empty live_variable flagged global */
          dataflow_live_variable_t *global_var = PROCESS_NEW(dataflow_live_variable_t);
          memset(global_var, 0, sizeof(dataflow_live_variable_t));
          global_var->global_id = dop->id;
          global_var->var_name = src->value.var_name;
          global_var->src.opcode_id = dop->id;
          global_var->src.operand_index = src_index;
          scarray_append(live_variables, global_var);

          fprintf(oplog, "Generate global var %s at 0x%x:%d\n", global_var->var_name,
                  dop->id.routine_hash, dop->id.op_index);
        }
      case DATAFLOW_VALUE_TYPE_THIS: /* FT */
      case DATAFLOW_VALUE_TYPE_TEMP:
        get_var_name(src, var_name, 256);
        //fprintf(oplog, "   [ searching live variables for %s (in operand %d.(%d->%d)) ]\n",
        //        var_name, dop->id.op_index, src_index, dst_index);
        while ((src_var = lookup_live_variable(live_variables, &lookup_index, src)) != NULL)
          add_predecessor(&src->predecessor, &src_var->src.opcode_id, src_var->src.operand_index);
      default: /*no*/;
    }

    if (add_predecessor(&dop->sink.predecessor, &dop->id, src_index)) {
      fprintf(oplog, "Link operand 0x%x:%d.%d into sink of type %d\n",
              dop->id.routine_hash, dop->id.op_index, src_index, dop->sink.type);
    }
  } else {
    bool found_any;
    dataflow_operand_t *src = get_dataflow_operand(dop, src_index);
    dataflow_operand_t *dst = get_dataflow_operand(dop, dst_index);
    dataflow_live_variable_t *src_var;

    switch (src->value.type) {
      case DATAFLOW_VALUE_TYPE_NONE:
      case DATAFLOW_VALUE_TYPE_CONST:
      case DATAFLOW_VALUE_TYPE_JMP: // it's a sink, though
      case DATAFLOW_VALUE_TYPE_INCLUDE:
      case DATAFLOW_VALUE_TYPE_FCALL:
        fprintf(oplog, "Nothing to link for 0x%x:%d.(%c->%c)\n", dop->id.routine_hash,
                dop->id.op_index, get_operand_index_code(src_index),
                get_operand_index_code(dst_index));
        break;
      case DATAFLOW_VALUE_TYPE_VAR:
      case DATAFLOW_VALUE_TYPE_THIS:
      case DATAFLOW_VALUE_TYPE_TEMP:
        found_any = false;
        get_var_name(src, var_name, 256);
        //fprintf(oplog, "   [ searching live variables for %s (in operand %d.(%d->%d)) ]\n",
        //        var_name, dop->id.op_index, src_index, dst_index);
        while ((src_var = lookup_live_variable(live_variables, &lookup_index, src)) != NULL) {
          found_any = true;
          // add_predecessor(&src->predecessor, &src_var->src.opcode_id, src_var->src.operand_index);
          if (add_predecessor(&dst->predecessor, &src_var->src.opcode_id,
                              src_var->src.operand_index)) {
            fprintf(oplog, "Link variable '%s': 0x%x:%d.%c -%d-> 0x%x:%d.%c\n",
                    var_name, src_var->src.opcode_id.routine_hash,
                    src_var->src.opcode_id.op_index,
                    get_operand_index_code(src_var->src.operand_index),
                    src_index, dop->id.routine_hash, dop->id.op_index,
                    get_operand_index_code(dst_index));
          }
        }
        if (!found_any)
          fprintf(oplog, "Can't find var '%s'\n", var_name);
        break;
    }

    if (commit)
      assign_destination(oplog, live_variables, dop, dst, dst_index);
  }
}

static void remove_live_variable(FILE *oplog, scarray_t *live_variables,
                                 dataflow_operand_t *doperand)
{
  char var_name[256];
  uint lookup_index = 0;
  dataflow_live_variable_t *dst_var;

  get_var_name(doperand, var_name, 256);
  while ((dst_var = lookup_live_variable(live_variables, &lookup_index, doperand)) != NULL) {
    scarray_remove(live_variables, lookup_index - 1 /* kind of hackish */);

    fprintf(oplog, "Remove variable '%s' at 0x%x:%d.%d\n",
            var_name, dst_var->src.opcode_id.routine_hash,
            dst_var->src.opcode_id.op_index, dst_var->src.operand_index);
  }
}

static void copy_dataflow_variables(scarray_t *dst, scarray_t *src)
{
  dataflow_live_variable_t *var, *copy;
  scarray_iterator_t *i;

  i = scarray_iterator_start(src); // todo: free these iterators with scarray_iterator_end()!
  while ((var = (dataflow_live_variable_t *) scarray_iterator_next(i)) != NULL) {
    copy = PROCESS_NEW(dataflow_live_variable_t);
    memcpy(copy, var, sizeof(dataflow_live_variable_t));
    scarray_append(dst, copy);
  }
}

static bool is_same_live_variable(dataflow_live_variable_t *first, dataflow_live_variable_t *second)
{
  if (first->is_temp != second->is_temp)
    return false;
  if (first->is_temp) {
    if (first->temp_id != second->temp_id)
      return false;
  } else {
    if (strcmp(first->var_name, second->var_name) != 0)
      return false;
  }
  if (first->src.opcode_id.routine_hash != second->src.opcode_id.routine_hash)
    return false;
  if (first->src.opcode_id.op_index != second->src.opcode_id.op_index)
    return false;
  if (first->src.operand_index != second->src.operand_index)
    return false;

  return true;
}

static void enqueue_dataflow_link_pass(FILE *oplog, dataflow_routine_t *droutine,
                                       uint start_index, scarray_t *live_variables)
{
  scarray_iterator_t *w;
  dataflow_link_pass_t *pass;

  fprintf(oplog, "Prepare dataflow link pass in 0x%x:%d\n",
          droutine->routine_hash, start_index);

  if (live_variables == NULL) {
    live_variables = PROCESS_NEW(scarray_t);
    scarray_init(live_variables);
  }

  /* if the pass is already prepared, merge in the `live_variables` */
  w = scarray_iterator_start(&dataflow_link_worklist);
  while ((pass = (dataflow_link_pass_t *) scarray_iterator_next(w)) != NULL) {
    if (pass->droutine->routine_hash == droutine->routine_hash && pass->start_index == start_index) {
      bool found, modified_variables = false; // merge live variables
      dataflow_live_variable_t *dst_var, *src_var;
      scarray_iterator_t *dst, *src = scarray_iterator_start(live_variables);
      while ((src_var = (dataflow_live_variable_t *) scarray_iterator_next(src)) != NULL) {
        found = false;
        dst = scarray_iterator_start(pass->live_variables);
        while ((dst_var = (dataflow_live_variable_t *) scarray_iterator_next(dst)) != NULL) {
          if (is_same_live_variable(dst_var, src_var)) {
            found = true;
            break;
          }
        }
        if (!found) {
          dst_var = PROCESS_NEW(dataflow_live_variable_t);
          memcpy(dst_var, src_var, sizeof(dataflow_live_variable_t));
          scarray_append(pass->live_variables, dst_var);
          modified_variables = true;
          dataflow_linking_complete = false;
        }
      }
      if (modified_variables && pass->complete)
        pass->complete = false;
      break;
    }
  }

  /* pass is not prepared yet, so prepare a new one now */
  if (pass == NULL) {
    pass = PROCESS_NEW(dataflow_link_pass_t);
    memset(pass, 0, sizeof(dataflow_link_pass_t));
    pass->live_variables = PROCESS_NEW(scarray_t);
    scarray_init(pass->live_variables);
    pass->droutine = droutine;
    pass->start_index = start_index;
    copy_dataflow_variables(pass->live_variables, live_variables);
    scarray_append(&dataflow_link_worklist, pass);
  }
}

static void enqueue_dataflow_link_include_pass(FILE *oplog, dataflow_routine_t *included_routine,
                                               dataflow_routine_t *resume_routine,
                                               uint resume_index,
                                               scarray_t *incoming_live_variables)
{
  dataflow_link_pass_t *pass = PROCESS_NEW(dataflow_link_pass_t);

  fprintf(oplog, "Prepare dataflow link pass for include 0x%x resuming at 0x%x:%d\n",
          included_routine->routine_hash, resume_routine->routine_hash, resume_index);

  memset(pass, 0, sizeof(dataflow_link_pass_t));
  pass->droutine = included_routine;
  pass->start_index = 0;
  pass->resume_droutine = resume_routine;
  pass->resume_index = resume_index;
  pass->live_variables = PROCESS_NEW(scarray_t);
  scarray_init(pass->live_variables);
  copy_dataflow_variables(pass->live_variables, incoming_live_variables);
  scarray_append(&dataflow_link_worklist, pass);
}

/*
void reset_dataflow_link_passes()
{
  scarray_iterator_t *w;
  dataflow_link_pass_t *pass;

  w = scarray_iterator_start(&dataflow_link_worklist);
  while ((pass = (dataflow_link_pass_t *) scarray_iterator_next(w)) != NULL) {
    pass->complete = false;
  }
}
*/

static void link_operand_dataflow(FILE *oplog, dataflow_link_pass_t *pass)
{
  bool end_pass = false;
  dataflow_opcode_t *dop;
  routine_cfg_t *croutine = cfg_routine_lookup(pass->droutine->app->cfg,
                                               pass->droutine->routine_hash);
  cfg_opcode_t *cop;
  scarray_iterator_t *i;

  if (croutine == NULL) {
    pass->complete = true;
    return;
  }

  fprintf(oplog, "Starting inner dataflow link pass 0x%x:%d\n",
          pass->droutine->routine_hash, pass->start_index);

  i = scarray_iterator_start_at(&pass->droutine->opcodes, pass->start_index);
  while (!end_pass && (dop = (dataflow_opcode_t *) scarray_iterator_next(i)) != NULL) {
    cop = (cfg_opcode_t *) routine_cfg_get_opcode(croutine, dop->id.op_index);

    switch (cop->opcode) {
      case ZEND_ASSIGN_ADD:
      case ZEND_ASSIGN_SUB:
      case ZEND_ASSIGN_MUL:
      case ZEND_ASSIGN_DIV:
      case ZEND_ASSIGN_MOD:
      case ZEND_ASSIGN_POW:
      case ZEND_ASSIGN_SL:
      case ZEND_ASSIGN_SR:
      case ZEND_ASSIGN_BW_OR:
      case ZEND_ASSIGN_BW_AND:
      case ZEND_ASSIGN_BW_XOR:
      case ZEND_ASSIGN_CONCAT:
      case ZEND_PRE_INC_OBJ: /* structural effect on 1.2; for now modeling as {1.2} => {1.2,r} */
      case ZEND_PRE_DEC_OBJ:
      case ZEND_POST_INC_OBJ:
      case ZEND_POST_DEC_OBJ:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_1, false);
      case ZEND_ASSIGN: /* FT */
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_1, true);
      case ZEND_ADD: /* FT */
      case ZEND_SUB:
      case ZEND_MUL:
      case ZEND_DIV:
      case ZEND_MOD:
      case ZEND_POW:
      case ZEND_SL:
      case ZEND_SR:
      case ZEND_BW_OR:
      case ZEND_BW_AND:
      case ZEND_BW_XOR:
      case ZEND_BW_NOT:
      case ZEND_CONCAT:
      case ZEND_FAST_CONCAT:
      case ZEND_BOOL_XOR:
      case ZEND_INSTANCEOF:
      case ZEND_IS_IDENTICAL:
      case ZEND_IS_NOT_IDENTICAL:
      case ZEND_IS_EQUAL:
      case ZEND_IS_NOT_EQUAL:
      case ZEND_IS_SMALLER:
      case ZEND_IS_SMALLER_OR_EQUAL:
      case ZEND_CASE:
      case ZEND_FETCH_DIM_R: /* {1.2} => {result} */
      case ZEND_FETCH_DIM_W:
      case ZEND_FETCH_DIM_RW:
      case ZEND_FETCH_DIM_IS:
      case ZEND_FETCH_OBJ_R:
      case ZEND_FETCH_OBJ_W:
      case ZEND_FETCH_OBJ_RW:
      case ZEND_FETCH_OBJ_IS:
      case ZEND_FETCH_CONSTANT:
      case ZEND_FETCH_DIM_UNSET: /* also {} => {1.2}, which is not modelled yet */
      case ZEND_FETCH_OBJ_UNSET:
      case ZEND_ROPE_END:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_RESULT, false);
      case ZEND_TYPE_CHECK: /* FT */
      case ZEND_DEFINED:
      case ZEND_CAST:
      case ZEND_QM_ASSIGN: /* ? */
      case ZEND_STRLEN:
      case ZEND_FE_RESET_R:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_ROPE_INIT:
      case ZEND_ROPE_ADD:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_1, true);
        break;
      case ZEND_BOOL:
      case ZEND_BOOL_NOT:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_ASSIGN_REF:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_1, false);
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_ASSIGN_OBJ:
      case ZEND_ASSIGN_DIM: /* ignoring key for now */
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_VALUE, DATAFLOW_OPERAND_MAP, true);
        break;
      case ZEND_ADD_ARRAY_ELEMENT:
        // if (op->op2_type != IS_UNUSED) /* when modelled, use the key in this case */
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_FE_FETCH_R:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_MAP, DATAFLOW_OPERAND_KEY, true);
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_MAP, DATAFLOW_OPERAND_VALUE, true);
        break;
      case ZEND_CLONE: /* also {1} => {opline} (potentially calls a clone() function) */
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_PRE_INC:
      case ZEND_PRE_DEC:
      case ZEND_POST_INC:
      case ZEND_POST_DEC:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_1, false);
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_FETCH_UNSET:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, false);
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_RESULT, true);
        /* also unsets {1.2} */
        break;
      case ZEND_UNSET_VAR:
        if (dop->op2.value.type == DATAFLOW_VALUE_TYPE_NONE)
          break; /* {} => {1} */
      case ZEND_UNSET_DIM: /* FT */
      case ZEND_UNSET_OBJ:
          break; /* {} => {1.2}, which is not modelled yet */
      case ZEND_ISSET_ISEMPTY_VAR:
        if (dop->op2.value.type != DATAFLOW_VALUE_TYPE_NONE)
          link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_RESULT, false);
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_ISSET_ISEMPTY_DIM_OBJ:
      case ZEND_ISSET_ISEMPTY_PROP_OBJ:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, false);
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_RESULT, true);
        break;
      /****************** sources *****************/
      case ZEND_FETCH_R:  /* fetch a superglobal */
      case ZEND_FETCH_W:
      case ZEND_FETCH_RW:
      case ZEND_FETCH_IS:
        if (dop->op1.value.type != DATAFLOW_VALUE_TYPE_NONE) {
          if (dop->source.id.routine_hash != 0)
            link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_SOURCE, DATAFLOW_OPERAND_RESULT, true);
          else
            link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        } else {
          link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_RESULT, false);
          link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
        }
        break;
      case ZEND_RECV:
      case ZEND_RECV_VARIADIC:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_SOURCE, DATAFLOW_OPERAND_RESULT, true);
        break;
      /****************** sinks *****************/
      case ZEND_ECHO:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_SINK, true);
        break;
      case ZEND_FREE:
        remove_live_variable(oplog, pass->live_variables, &dop->op1);
        break;
      case ZEND_ADD_INTERFACE: /* also {2.zv+1} => {code} */
      case ZEND_ADD_TRAIT:     /* also {2.zv+1} => {code} */
      case ZEND_DECLARE_INHERITED_CLASS_DELAYED:
      case ZEND_DECLARE_CONST:
      case ZEND_BIND_GLOBAL:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_2, DATAFLOW_OPERAND_SINK, false);
      case ZEND_SEND_VAL: /* FT */
      case ZEND_SEND_VAL_EX:
      case ZEND_SEND_VAR:
      case ZEND_SEND_VAR_NO_REF:
      case ZEND_SEND_REF:
      case ZEND_SEND_USER:
      case ZEND_SEND_ARRAY:
      case ZEND_DECLARE_CLASS:
      case ZEND_DECLARE_INHERITED_CLASS:
      case ZEND_BIND_TRAITS:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_SINK, true);
        break;
      case ZEND_DECLARE_FUNCTION: /* {compiler} =i=> {code} */
        break;
      /****************** branches *****************/
      case ZEND_JMP:
        enqueue_dataflow_link_pass(oplog, pass->droutine, dop->op1.value.jmp_target,
                                   pass->live_variables);
        end_pass = true;
        break;
      case ZEND_JMPZ:
      case ZEND_JMPNZ:
      case ZEND_JMPZNZ:
      case ZEND_JMPZ_EX:
      case ZEND_JMPNZ_EX:
        enqueue_dataflow_link_pass(oplog, pass->droutine, dop->op2.value.jmp_target,
                                   pass->live_variables);
        break;
      case ZEND_INIT_METHOD_CALL: /* {1.2} => {fcall-stack} */
        break;
      case ZEND_INIT_STATIC_METHOD_CALL: /* {1.2} => {fcall-stack} */
        break;
      case ZEND_INIT_FCALL: /* {2} => {fcall-stack} */
      case ZEND_INIT_FCALL_BY_NAME:
      case ZEND_INIT_USER_CALL:
      case ZEND_INIT_NS_FCALL_BY_NAME:
        break;
      case ZEND_NEW: /* {1} =d=> {fcall-stack} (or skip via 2 => opline if no ctor) */
        break;
      case ZEND_DO_FCALL:
      case ZEND_DO_ICALL:
        // if (is_db_sink_function(NULL, id.call_target))    /* {fcall-stack} => {db,opline,result} */
        // else if (is_file_sink_function(id.call_target))   /* {fcall-stack} => {file,opline,result} */
        // else if (is_system_sink_function(id.call_target)) /* {fcall-stack} => {system,opline,result} */
        // else                                              /* {fcall-stack} =d=> {opline,result} */
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_SOURCE, DATAFLOW_OPERAND_RESULT, true);
        break;
      case ZEND_RETURN:
      case ZEND_RETURN_BY_REF:
        link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_SINK, true);
        break;
      case ZEND_INCLUDE_OR_EVAL:
        switch (cop->extended_value) {
          case ZEND_EVAL: /* {1} =d=> {code,opline} */
            break;
          case ZEND_INCLUDE:
          case ZEND_REQUIRE: {
            dataflow_routine_t *included_routine = sctable_lookup(&dg->routine_table,
                                                                  dop->source.include.routine_hash);
            dataflow_link_pass_t include_pass = { false, included_routine, 0, NULL, 0,
                                                  pass->live_variables };
            link_operand_dataflow(oplog, &include_pass);
          } break;
          case ZEND_INCLUDE_ONCE: /* ??? {1} =d=> {opline}, {file} =d=> {code} */
          case ZEND_REQUIRE_ONCE: {
            uint resume_index = scarray_iterator_index(&pass->droutine->opcodes, i);
            dataflow_routine_t *included_routine = sctable_lookup(&dg->routine_table,
                                                                  dop->source.include.routine_hash);
            enqueue_dataflow_link_include_pass(oplog, included_routine, pass->droutine,
                                               resume_index, pass->live_variables);
          } break;
        }
        break;
      case ZEND_THROW: /* {1} =d=> {fast-ret,thrown} */
        break;
      case ZEND_HANDLE_EXCEPTION: /* {thrown} =i=> {opline} */
        break;
      case ZEND_DISCARD_EXCEPTION: /* {thrown} =d=> {thrown} */
        break;
      case ZEND_FAST_CALL: /* {thrown} =d=> {fast-ret,opline} */
        break;
      case ZEND_FAST_RET: /* {fast-ret} =i=> {opline} */
        break;
      case ZEND_EXIT:
        if (dop->op1.value.type == DATAFLOW_VALUE_TYPE_NONE)
          break; /* {} => {opline} */
        else
          link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_SINK, true);
          /* also {} =d=> {opline} */
        break;
      case ZEND_JMP_SET: /* nop if op1 is false, but can't see from here */
        if (dop->op1.value.type != DATAFLOW_VALUE_TYPE_CONST ||
            dop->op1.value.constant->u1.v.type != IS_FALSE) {
          link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
          /* also {1} => {opline} */
        }
        break;
      case ZEND_COALESCE: /* nop if op1 is null, but can't see from here */
        if (dop->op1.value.type != DATAFLOW_VALUE_TYPE_CONST ||
            dop->op1.value.constant->u1.v.type != IS_NULL) {
          link_operand(oplog, pass->live_variables, dop, DATAFLOW_OPERAND_1, DATAFLOW_OPERAND_RESULT, true);
          /* also {1} => {opline} */
        }
        break;
      case ZEND_BEGIN_SILENCE:
      case ZEND_END_SILENCE:
        print_sink(oplog, "sink(internal) {} =d=> {error-reporting}");
        break;
    }
    if (end_pass)
      break;
  }
  scarray_iterator_end(i);

  if (pass->resume_droutine != NULL) {
    dataflow_link_pass_t resume_pass = { false, pass->resume_droutine, pass->resume_index, NULL, 0,
                                         pass->live_variables };
    link_operand_dataflow(oplog, &resume_pass);
  }
  pass->complete = true;
}

bool propagate_one_source(dataflow_influence_t **influence, dataflow_source_t *source)
{
  dataflow_influence_t *dst_influence = *influence;

  while (dst_influence != NULL) {
    if (is_same_opcode_id(&dst_influence->source.id, &source->id))
      return false;
    dst_influence = dst_influence->next;
  }

  dst_influence = PROCESS_NEW(dataflow_influence_t);
  dst_influence->source = *source;
  dst_influence->next = *influence;
  *influence = dst_influence;

  return true;
}

bool propagate_operand_source_dataflow(FILE *oplog, dataflow_opcode_t *dst_op,
                                       dataflow_predecessor_t **dst_predecessor,
                                       dataflow_influence_t **dst_influence, const char *tag)
{
  bool propagated = false;
  dataflow_opcode_t *src_op;
  dataflow_routine_t *src_routine;
  dataflow_operand_t *src_operand;
  dataflow_influence_t *src_influence;
  dataflow_predecessor_t *p = *dst_predecessor;

  while (p != NULL) {
    if (p->operand_id.operand_index == DATAFLOW_OPERAND_SOURCE) {
      dataflow_routine_t *source_routine = sctable_lookup(&dg->routine_table,
                                                          p->operand_id.opcode_id.routine_hash);
      if (source_routine != NULL) {
        src_op = (dataflow_opcode_t *) source_routine->opcodes.data[p->operand_id.opcode_id.op_index];
        if (propagate_one_source(dst_influence, &src_op->source)) {
          propagated = true;
          source_propagation_complete = false;
        }
      }
    } else {
      src_routine = sctable_lookup(&dg->routine_table, p->operand_id.opcode_id.routine_hash);
      if (src_routine != NULL) {
        src_op = (dataflow_opcode_t *) src_routine->opcodes.data[p->operand_id.opcode_id.op_index];
        src_operand = get_dataflow_operand(src_op, p->operand_id.operand_index);

        src_influence = src_operand->influence;
        while (src_influence != NULL) {
          if (propagate_one_source(dst_influence, &src_influence->source)) {
            propagated = true;
            source_propagation_complete = false;
            fprintf(oplog, "[%s] Propagated source at 0x%x:%d from 0x%x:%d to 0x%x:%d\n",
                    tag, src_influence->source.id.routine_hash, src_influence->source.id.op_index,
                    src_op->id.routine_hash, src_op->id.op_index,
                    dst_op->id.routine_hash, dst_op->id.op_index);
          }
          src_influence = src_influence->next;
        }
      }
    }
    p = p->next;
  }
  return propagated;
}

void propagate_source_dataflow(FILE *oplog, dataflow_link_pass_t *pass)
{
  // bool end_pass = false; // needed?
  dataflow_opcode_t *dop;
  scarray_iterator_t *i;
  routine_cfg_t *croutine = cfg_routine_lookup(pass->droutine->app->cfg,
                                               pass->droutine->routine_hash);

  if (croutine == NULL)
    return;

  fprintf(oplog, "Starting inner source propagation pass at 0x%x:%d\n",
          pass->droutine->routine_hash, pass->start_index);

  i = scarray_iterator_start_at(&pass->droutine->opcodes, pass->start_index);
  while (/* !end_pass && */ (dop = (dataflow_opcode_t *) scarray_iterator_next(i)) != NULL) {
    cfg_opcode_t *cop = (cfg_opcode_t *) routine_cfg_get_opcode(croutine, dop->id.op_index);
    if (cop->opcode == ZEND_INCLUDE_OR_EVAL) {
      switch (cop->extended_value) {
        case ZEND_INCLUDE:
        case ZEND_REQUIRE: {
          dataflow_routine_t *included_routine = sctable_lookup(&dg->routine_table,
                                                                dop->source.include.routine_hash);
          dataflow_link_pass_t include_pass = { false, included_routine, 0, NULL, 0,
                                                pass->live_variables };
          propagate_source_dataflow(oplog, &include_pass);
        } break;
        default: /*no*/;
      }
    }
    propagate_operand_source_dataflow(oplog, dop, &dop->op1.predecessor, &dop->op1.influence, "op1");
    propagate_operand_source_dataflow(oplog, dop, &dop->op2.predecessor, &dop->op2.influence, "op2");
    propagate_operand_source_dataflow(oplog, dop, &dop->result.predecessor, &dop->result.influence, "result");
    if (propagate_operand_source_dataflow(oplog, dop, &dop->sink.predecessor, &dop->sink.influence, "sink")) {
      fprintf(oplog, "Propagated at least one source to sink at op 0x%x:%d\n",
              pass->droutine->routine_hash, dop->id.op_index);
    }
  }
  scarray_iterator_end(i);

  if (pass->resume_droutine != NULL) {
    dataflow_link_pass_t resume_pass = { false, pass->resume_droutine, pass->resume_index,
                                         NULL, 0, pass->live_variables };
    propagate_source_dataflow(oplog, &resume_pass);
  }
}

static void propagate_function_dataflow(FILE *oplog, scarray_t *call_stack,
                                        dataflow_routine_t *routine)
{
  scarray_iterator_t *c, *l, *f, *s, *t;
  dataflow_sink_t *callee_sink, *caller_sink, *arg;
  dataflow_influence_t *influence, *arg_influence, *result_influence;
  dataflow_call_site_t *call, *callee_call;
  dataflow_routine_t *callee;
  uint frame_hash;
  bool recursion;

  if (routine->call_sites == NULL)
    return;

  fprintf(oplog, "Starting inner function propagation pass in 0x%x with %d call sites\n",
          routine->routine_hash, routine->call_sites->size);

  scarray_append(call_stack, (void *) (uint64) routine->routine_hash);

  c = scarray_iterator_start(routine->call_sites);
  while ((call = (dataflow_call_site_t *) scarray_iterator_next(c)) != NULL) {
    if ((call->flags & DATAFLOW_CALL_FLAG_BY_NAME) > 0) {
      fprintf(oplog, "Skipping call by name from 0x%x to %s (for now)\n",
              routine->routine_hash, call->call_by_name.name);
      continue;
    }
    f = scarray_iterator_start(call_stack);
    recursion = false;
    while ((frame_hash = (uint) (uint64) scarray_iterator_next(f)) != 0) {
      if (call->target_hash == frame_hash) {
        fprintf(oplog, "Skipping call from 0x%x to 0x%x to avoid recursion\n",
                routine->routine_hash, call->target_hash);
        recursion = true;
        break;
      }
    }
    if (recursion)
      continue;

    callee = (dataflow_routine_t *) sctable_lookup(&dg->routine_table, call->target_hash);
    if (callee == NULL)
      continue;

    s = scarray_iterator_start(&callee->sinks);
    while ((callee_sink = (dataflow_sink_t *) scarray_iterator_next(s)) != NULL) {
      influence = callee_sink->influence;
      while (influence != NULL) {
        if (influence->source.type == SOURCE_TYPE_PARAMETER) {
          arg = (dataflow_sink_t *) call->args->data[influence->source.parameter_index];
          arg_influence = arg->influence;
          while (arg_influence != NULL) { // pushes behind the `influence` pointer
            fprintf(oplog, "Propagating arg %d influence\n",
                    influence->source.parameter_index);
            if (propagate_one_source(&callee_sink->influence, &arg_influence->source)) {
              function_propagation_complete = false;
              fprintf(oplog, "Propagated source at 0x%x:%d via 0x%x(#%d) to 0x%x:%d\n",
                      arg_influence->source.id.routine_hash, arg_influence->source.id.op_index,
                      call->target_hash, influence->source.parameter_index,
                      callee_sink->id.routine_hash, callee_sink->id.op_index);
            }
            arg_influence = arg_influence->next;
          }
        }
        influence = influence->next;
      }

      if (callee_sink->type == SINK_TYPE_RESULT) {
        dataflow_opcode_t *call_op = routine->opcodes.data[call->opcode_index];

        l = scarray_iterator_start(&routine->sinks);
        while ((caller_sink = (dataflow_sink_t *) scarray_iterator_next(l)) != NULL) {
          influence = caller_sink->influence;
          while (influence != NULL) {
            if (influence->source.id.routine_hash == call_op->source.id.routine_hash &&
                influence->source.id.op_index == call_op->source.id.op_index) {
              result_influence = callee_sink->influence;
              while (result_influence != NULL) { // pushes behind the `influence` pointer
                if (propagate_one_source(&caller_sink->influence, &result_influence->source)) {
                  function_propagation_complete = false;
                  fprintf(oplog,
                          "Propagated source at 0x%x:%d via 0x%x:result to 0x%x:%d\n",
                          result_influence->source.id.routine_hash,
                          result_influence->source.id.op_index,
                          callee->routine_hash,
                          caller_sink->id.routine_hash, caller_sink->id.op_index);
                }
                result_influence = result_influence->next;
              }
            }
            influence = influence->next;
          }
        }
      }
    }

    if (callee->call_sites != NULL) {
      t = scarray_iterator_start(callee->call_sites);
      while ((callee_call = (dataflow_call_site_t *) scarray_iterator_next(t)) != NULL) {
        callee = (dataflow_routine_t *) sctable_lookup(&dg->routine_table, callee_call->target_hash);
        propagate_function_dataflow(oplog, call_stack, callee);
      }
    }
  }

  // also need to follow function results (including pass-by-refs) to the caller's sinks

  scarray_remove(call_stack, call_stack->size-1);
}

int analyze_dataflow(application_t *app, zend_file_handle *file)
{
  int retval;
  scarray_iterator_t *i;
  dataflow_link_pass_t *pass;
  scarray_t call_stack;
  dataflow_file_t top_file;
  dataflow_file_t *next_file;
  dataflow_routine_t *droutine;
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;

  SPOT("static_dataflow() for file %s\n", file->filename);

  dg = PROCESS_NEW(dataflow_graph_t);
  dg->top_routine = NULL;
  dg->routine_table.hash_bits = 9;
  sctable_init(&dg->routine_table);
  scarray_init(&dg->routine_list);
  scarray_init(&dataflow_link_worklist);

  retval = analyze_file_dataflow(file, &top_file);
  analyzing_include = true; // all other files must be includes

  while (retval == SUCCESS) {
    zend_file_handle file;

    next_file = dataflow_file_list;
    while (next_file != NULL) {
      if (!next_file->analyzed)
        break;
      next_file = next_file->next;
    }

    if (next_file == NULL)
      break;

    next_file->analyzed = true;
    retval = zend_stream_open(next_file->path, &file);
    if (retval != SUCCESS) {
      ERROR("Failed to locate included file '%s'\n", next_file->path);
      continue; // allow bad includes
    }

    retval = analyze_file_dataflow(&file, next_file);
  }

  i = scarray_iterator_start(&dg->routine_list);
  while ((droutine = (dataflow_routine_t *) scarray_iterator_next(i)) != NULL)
    enqueue_dataflow_link_pass(oplog, droutine, 0, NULL);
  scarray_iterator_end(i);

  dataflow_linking_complete = false;
  while (!dataflow_linking_complete) {
    fprintf(oplog, "Starting outer dataflow link pass\n");
    dataflow_linking_complete = true;
    while (true) {
      i = scarray_iterator_start(&dataflow_link_worklist);
      while ((pass = (dataflow_link_pass_t *) scarray_iterator_next(i)) != NULL) {
        if (!pass->complete)
          break;
      }
      scarray_iterator_end(i);
      if (pass == NULL)
        break;
      else
        link_operand_dataflow(oplog, pass);
    }
  }

  /*
  i = scarray_iterator_start(&dg->routine_list);
  while ((droutine = (dataflow_routine_t *) scarray_iterator_next(i)) != NULL)
    prepare_source_propagation_pass(droutine, 0, NULL);
  scarray_iterator_end(i);
  */

  source_propagation_complete = false;
  //reset_dataflow_link_passes(); // ignoring completeness of individual passes for now
  while (!source_propagation_complete) {
    fprintf(oplog, "Starting outer source propagation pass\n");
    source_propagation_complete = true;
    i = scarray_iterator_start(&dataflow_link_worklist);
    while ((pass = (dataflow_link_pass_t *) scarray_iterator_next(i)) != NULL)
      propagate_source_dataflow(oplog, pass);
  }

  function_propagation_complete = false;
  scarray_init(&call_stack);
  while (!function_propagation_complete) {
    fprintf(oplog, "Starting outer function propagation pass in 0x%x\n",
            dg->top_routine->routine_hash);
    function_propagation_complete = true;
    propagate_function_dataflow(oplog, &call_stack, dg->top_routine);
    if (call_stack.size > 0)
      ERROR("Failed to correctly unwind the call stack during function propagation\n");
  }

  if (top_file.zops != NULL) {
    destroy_op_array(top_file.zops TSRMLS_CC);
    efree(top_file.zops);
  }
  next_file = dataflow_file_list;
  while (next_file != NULL) {
    dataflow_file_list = next_file->next;
    if (next_file->zops != NULL) {
      destroy_op_array(next_file->zops TSRMLS_CC);
      efree(next_file->zops);
    }
    PROCESS_FREE(next_file->path);
    PROCESS_FREE(next_file);
    next_file = dataflow_file_list;
  }

  return retval;
}

void destroy_dataflow_analysis()
{
  if (dg != NULL) {
    sctable_destroy(&dg->routine_table);
    scarray_destroy(&dg->routine_list);
    PROCESS_FREE(dg);
  }
}

void add_dataflow_routine(application_t *app, uint routine_hash, zend_op_array *zops,
                          bool is_function)
{
  dataflow_routine_t *routine = PROCESS_NEW(dataflow_routine_t);
  routine->app = app;
  routine->routine_hash = routine_hash;
  scarray_init(&routine->opcodes);
  scarray_init(&routine->sources);
  scarray_init(&routine->sinks);
  sctable_add(&dg->routine_table, routine_hash, routine);

  if (is_function || !analyzing_include)
    scarray_append(&dg->routine_list, routine);

  assembling_routine = routine;

  if (dg->top_routine == NULL && !is_function)
    dg->top_routine = routine;
}

static void initilize_dataflow_operand(dataflow_operand_t *operand, zend_op_array *zops,
                                       znode_op *znode, zend_uchar type)
{
  switch (type) {
    case IS_CONST:
      operand->value.type = DATAFLOW_VALUE_TYPE_CONST;
      operand->value.constant = RT_CONSTANT(zops, *znode);
      break;
    case IS_VAR:
    case IS_TMP_VAR:
      operand->value.type = DATAFLOW_VALUE_TYPE_TEMP;
      operand->value.temp_id = (uint) (EX_VAR_TO_NUM(znode->var) - zops->last_var);
      break;
    case IS_CV:
      operand->value.var_name = zops->vars[EX_VAR_TO_NUM(znode->var)]->val;
      if (strcmp("this", operand->value.var_name) == 0)
        operand->value.type = DATAFLOW_VALUE_TYPE_THIS;
      else
        operand->value.type = DATAFLOW_VALUE_TYPE_VAR;
      break;
    case IS_UNUSED:
    default:
      operand->value.type = DATAFLOW_VALUE_TYPE_NONE;
  }
}

static void initialize_opcode_index(uint routine_hash, uint index, dataflow_routine_t **routine_out,
                                    dataflow_opcode_t **opcode_out)
{
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;

  if (assembling_routine != NULL && assembling_routine->routine_hash == routine_hash)
    routine = assembling_routine;
  else
    routine = sctable_lookup(&dg->routine_table, routine_hash);

  while (index >= routine->opcodes.size) {
    opcode = PROCESS_NEW(dataflow_opcode_t);
    memset(opcode, 0, sizeof(dataflow_opcode_t));
    scarray_append(&routine->opcodes, opcode);
  }

  opcode = (dataflow_opcode_t *) scarray_get(&routine->opcodes, index);
  opcode->id.routine_hash = routine_hash;
  opcode->id.op_index = index;

  *routine_out = routine;
  *opcode_out = opcode;
}

static inline void initialize_sink(dataflow_opcode_t *dop, dataflow_sink_type_t type)
{
  dop->sink.id = dop->id;
  dop->sink.type = type;
}

static inline void initialize_source(dataflow_opcode_t *opcode, dataflow_source_type_t type)
{
  opcode->source.id = opcode->id;
  opcode->source.type = type;
}

void add_dataflow_opcode(uint routine_hash, uint index, zend_op_array *zops)
{
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;
  zend_op *zop = &zops->opcodes[index];

  initialize_opcode_index(routine_hash, index, &routine, &opcode);

  if (routine_hash == 0x2f361fa && index == 0xd)
    printf("wait\n");

  switch (zop->opcode) {
    case ZEND_JMP:
      opcode->op1.value.type = DATAFLOW_VALUE_TYPE_JMP;
      opcode->op1.value.jmp_target = (uint) (OP_JMP_ADDR(zop, zop->op1) - zops->opcodes);
      break;
    case ZEND_JMPZ:
    case ZEND_JMPNZ:
    case ZEND_JMPZNZ:
    case ZEND_JMPZ_EX:
    case ZEND_JMPNZ_EX:
      opcode->op2.value.type = DATAFLOW_VALUE_TYPE_JMP;
      opcode->op2.value.jmp_target = (uint) (OP_JMP_ADDR(zop, zop->op2) - zops->opcodes);
      break;
    default:
      initilize_dataflow_operand(&opcode->op1, zops, &zop->op1, zop->op1_type);
      initilize_dataflow_operand(&opcode->op2, zops, &zop->op2, zop->op2_type);
      initilize_dataflow_operand(&opcode->result, zops, &zop->result, zop->result_type);
  }

  switch (zop->opcode) {
    // where do these happen? SOURCE_TYPE_FILE|SOURCE_TYPE_SQL|SOURCE_TYPE_SYSTEM (via fcall)
    case ZEND_ECHO:
      initialize_sink(opcode, SINK_TYPE_OUTPUT);
      break;
    case ZEND_JMPZ:
    case ZEND_JMPNZ:
    case ZEND_JMPZNZ:
    case ZEND_JMPZ_EX:
    case ZEND_JMPNZ_EX:
      initialize_sink(opcode, SINK_TYPE_EDGE);
      break;
    case ZEND_JMP_SET:
      // dunno how this works
      break;
    case ZEND_DO_FCALL:
    case ZEND_DO_ICALL:
      initialize_source(opcode, SOURCE_TYPE_RESULT);
      break;
    case ZEND_COALESCE:
      // dunno how this works
      break;
    case ZEND_INCLUDE_OR_EVAL:
      if (zop->extended_value == ZEND_EVAL) // else handled in add_dataflow_include
        initialize_sink(opcode, SINK_TYPE_CODE);
      break;
    case ZEND_DECLARE_CLASS:
    case ZEND_DECLARE_INHERITED_CLASS:
    case ZEND_DECLARE_INHERITED_CLASS_DELAYED:
    case ZEND_DECLARE_FUNCTION:
    case ZEND_ADD_INTERFACE:
    case ZEND_ADD_TRAIT:
    case ZEND_BIND_TRAITS:
      initialize_sink(opcode, SINK_TYPE_CODE);
      break;
    case ZEND_FETCH_R:  /* fetch a superglobal */
    case ZEND_FETCH_W:
    case ZEND_FETCH_RW:
    case ZEND_FETCH_IS:
      if (zop->op2_type == IS_UNUSED) {
        const char *superglobal_name = Z_STRVAL_P(RT_CONSTANT(zops, zop->op1));
        if (superglobal_name != NULL) {
          if (strcmp(superglobal_name, "_SESSION") == 0) {
            initialize_source(opcode, SOURCE_TYPE_SESSION);
            break;
          } else if (strcmp(superglobal_name, "_REQUEST") == 0 ||
                     strcmp(superglobal_name, "_GET") == 0 ||
                     strcmp(superglobal_name, "_POST") == 0 ||
                     strcmp(superglobal_name, "_COOKIE") == 0 ||
                     strcmp(superglobal_name, "_FILES") == 0) {
            initialize_source(opcode, SOURCE_TYPE_HTTP);
            break;
          } else if (strcmp(superglobal_name, "_ENV") == 0 ||
                     strcmp(superglobal_name, "_SERVER") == 0) {
            initialize_source(opcode, SOURCE_TYPE_SYSTEM);
          }
        }
      }
      break;
    case ZEND_SEND_VAL:
    case ZEND_SEND_VAL_EX:
    case ZEND_SEND_VAR:
    case ZEND_SEND_VAR_NO_REF:
    case ZEND_SEND_REF:
    case ZEND_SEND_USER:
    case ZEND_SEND_ARRAY:
      initialize_sink(opcode, SINK_TYPE_CALL);
      break;
    case ZEND_RECV:
    case ZEND_RECV_VARIADIC:
      initialize_source(opcode, SOURCE_TYPE_PARAMETER);

      if (routine->parameters == NULL) {
        routine->parameters = PROCESS_NEW(scarray_t);
        scarray_init(routine->parameters);
      }
      opcode->source.parameter_index = routine->parameters->size;
      scarray_append(routine->parameters, &opcode->sink);
      break;
    case ZEND_RETURN:
    case ZEND_RETURN_BY_REF:
      initialize_sink(opcode, SINK_TYPE_RESULT);
      break;
    case ZEND_DECLARE_CONST:
    case ZEND_BIND_GLOBAL:
      initialize_source(opcode, SOURCE_TYPE_GLOBAL);
      initialize_sink(opcode, SINK_TYPE_GLOBAL);
      break;
    case ZEND_EXIT:
      if (zop->op1_type != IS_UNUSED)
        initialize_sink(opcode, SINK_TYPE_EXIT_CODE);
      break;
  }

  if (opcode->source.id.routine_hash != 0)
    scarray_append(&routine->sources, &opcode->source);
  if (opcode->sink.id.routine_hash != 0)
    scarray_append(&routine->sinks, &opcode->sink);
}

void push_dataflow_fcall_init(application_t *app, uint target_hash,
                              const char *routine_name/*must copy*/)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;
  dataflow_call_site_t *call = PROCESS_NEW(dataflow_call_site_t);
  memset(call, 0, sizeof(dataflow_call_site_t));
  call->target_hash = target_hash;
  scarray_append(&fcall_stack, call);

  fprintf(oplog, "Init fcall to %s (0x%x)\n", routine_name, target_hash);
}

void add_dataflow_fcall(application_t *app, uint routine_hash, uint index, zend_op_array *zops,
                        const char *routine_name)
{
  FILE *oplog = ((cfg_files_t *) app->cfg_files)->opcode_log;
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;
  zend_op *zop = &zops->opcodes[index];
  dataflow_call_site_t *call = (dataflow_call_site_t *) scarray_remove(&fcall_stack,
                                                                       fcall_stack.size-1);

  call->opcode_index = index;
  initialize_opcode_index(routine_hash, index, &routine, &opcode);
  initilize_dataflow_operand(&opcode->result, zops, &zop->result, zop->result_type);

  // if (?) call->flags |= DATAFLOW_CALL_FLAG_BY_NAME;

  if (is_db_source_function(NULL, routine_name)) {
    initialize_source(opcode, SOURCE_TYPE_SQL);
    call->flags |= DATAFLOW_CALL_FLAG_BUILTIN;
  } else if (is_db_sink_function(NULL, routine_name)) {
    initialize_sink(opcode, SINK_TYPE_SQL);
    call->flags |= DATAFLOW_CALL_FLAG_BUILTIN;
  } else if (is_file_source_function(routine_name)) {
    initialize_source(opcode, SOURCE_TYPE_FILE);
    call->flags |= DATAFLOW_CALL_FLAG_BUILTIN;
  } else if (is_file_sink_function(routine_name)) {
    initialize_sink(opcode, SINK_TYPE_FILE);
    call->flags |= DATAFLOW_CALL_FLAG_BUILTIN;
  } else if (is_system_source_function(routine_name)) {
    initialize_source(opcode, SOURCE_TYPE_SYSTEM);
    call->flags |= DATAFLOW_CALL_FLAG_BUILTIN;
  } else if (is_system_sink_function(routine_name)) {
    initialize_sink(opcode, SINK_TYPE_SYSTEM);
    call->flags |= DATAFLOW_CALL_FLAG_BUILTIN;
  }

  if (routine->call_sites == NULL) {
    routine->call_sites = PROCESS_NEW(scarray_t);
    scarray_init(routine->call_sites);
  }
  scarray_append(routine->call_sites, call);

  fprintf(oplog, "Routine 0x%x makes a call to %s with %d args\n",
          routine->routine_hash, call->call_by_name.name,
          call->args == NULL ? 0 : call->args->size);
}

void add_dataflow_fcall_arg(uint routine_hash, uint index, zend_op_array *zops,
                        const char *routine_name)
{
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;
  zend_op *zop = &zops->opcodes[index];
  dataflow_call_site_t *call = (dataflow_call_site_t *) fcall_stack.data[fcall_stack.size-1];

  initialize_opcode_index(routine_hash, index, &routine, &opcode);
  initilize_dataflow_operand(&opcode->op1, zops, &zop->op1, zop->op1_type);

  if (call->args == NULL) {
    call->args = PROCESS_NEW(scarray_t);
    scarray_init(call->args);
  }

  opcode->sink.id.routine_hash = routine_hash;
  opcode->sink.id.op_index = index;
  opcode->sink.type = SINK_TYPE_CALL;

  // when? call->flags |= DATAFLOW_CALL_FLAG_BY_NAME;
  // when? call->call_by_name.name = routine_name;
  scarray_append(call->args, &opcode->sink);
}

void add_dataflow_map_assignment(uint routine_hash, uint index, zend_op_array *zops)
{
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;
  zend_op *zop1 = &zops->opcodes[index];
  zend_op *zop2 = &zops->opcodes[index+1];

  initialize_opcode_index(routine_hash, index, &routine, &opcode);

  if (zop1->op1_type == IS_UNUSED) {
    opcode->map.value.type = DATAFLOW_VALUE_TYPE_THIS;
    opcode->map.value.class = zops->scope; // do we need zend_op_array.this_var (index of CV)?
  } else {
    initilize_dataflow_operand(&opcode->map, zops, &zop1->op1, zop1->op1_type);
  }
  initilize_dataflow_operand(&opcode->key, zops, &zop1->op2, zop1->op2_type);
  initilize_dataflow_operand(&opcode->value, zops, &zop2->op1, zop2->op1_type);
}

void add_dataflow_foreach_fetch(uint routine_hash, uint index, zend_op_array *zops)
{
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;
  zend_op *zop1 = &zops->opcodes[index];
  zend_op *zop2 = &zops->opcodes[index+1];

  initialize_opcode_index(routine_hash, index, &routine, &opcode);

  if (zop1->op1_type == IS_UNUSED) {
    opcode->map.value.type = DATAFLOW_VALUE_TYPE_THIS;
    opcode->map.value.class = zops->scope; // do we need zend_op_array.this_var (index of CV)?
  } else {
    initilize_dataflow_operand(&opcode->map, zops, &zop1->op1, zop1->op1_type);
  }
  initilize_dataflow_operand(&opcode->key, zops, &zop2->result, zop2->result_type);
  initilize_dataflow_operand(&opcode->value, zops, &zop1->result, zop1->result_type);
}

void add_dataflow_include(uint routine_hash, uint index, zend_op_array *zops,
                          const char *include_path, uint include_hash)
{
  dataflow_routine_t *routine;
  dataflow_opcode_t *opcode;
  dataflow_file_t *file = dataflow_file_list;
  bool found = false;

  initialize_opcode_index(routine_hash, index, &routine, &opcode);
  initialize_source(opcode, SOURCE_TYPE_INCLUDE);
  opcode->source.include.routine_hash = include_hash;

  SPOT("Add static dataflow include %s\n", include_path);

  while (file != NULL) {
    if (strcmp(include_path, file->path) == 0) {
      found = true;
      break;
    }
    file = file->next;
  }

  if (found)
    return;

  file = PROCESS_NEW(dataflow_file_t);
  file->path = strdup(include_path);
  file->analyzed = false;
  file->next = dataflow_file_list;
  dataflow_file_list = file;
}
