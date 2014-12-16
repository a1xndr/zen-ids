#include "php.h"
#include "php_opcode_monitor.h"
#include "lib/script_cfi_utils.h"
#include "interp_context.h"

typedef struct _interp_context_t {
  const char *name;
  uint id;
  cfg_t *cfg;
} interp_context_t;

typedef struct _shadow_frame_t {
  zend_op *op;
  uint continuation_index;
  interp_context_t context;
} shadow_frame_t;

static shadow_frame_t shadow_stack[256];
static shadow_frame_t *shadow_frame;
static shadow_frame_t *last_pop;

static interp_context_t current_context = { "<root>", 0, NULL };
//static interp_context_t staged_context = { NULL, 0, NULL };
static interp_context_t last_context = { NULL, 0, NULL }; // debug only, valid only after pop_context()

void initialize_interp_context()
{
  shadow_frame = shadow_stack;
  shadow_frame->op = NULL;
  last_pop = NULL;
}

void push_interp_context(zend_op* op, uint branch_index, cfg_t *cfg)
{
  //ASSERT(staged_context.name != NULL);
  
  shadow_frame->op = op;
  shadow_frame->continuation_index = branch_index + 1;
  shadow_frame->context = current_context;
  
  shadow_frame++;
  //current_context = staged_context;
  current_context.cfg = cfg;
  //staged_context.name = NULL;
}

void set_interp_cfg(cfg_t *cfg)
{
  current_context.cfg = cfg;
}

void pop_interp_context()
{
  last_pop = --shadow_frame;
  last_context = current_context;
  current_context = shadow_frame->context;
}

void verify_interp_context(zend_op* head, zend_op *op, uint index)
{
  shadow_frame_t *verify_frame;
  
  if (last_pop == NULL) {
    switch (op->opcode) {
      case ZEND_INCLUDE_OR_EVAL:
      case ZEND_DECLARE_FUNCTION:
      case ZEND_DECLARE_LAMBDA_FUNCTION:
      case ZEND_RECV:
        break;
      default:
        if (op->opcode != current_context.cfg->opcodes[index]) {
          PRINT("Error! Expected opcode %s at index %d, but found opcode %s\n", 
                zend_get_opcode_name(current_context.cfg->opcodes[index]), index,
                zend_get_opcode_name(op->opcode));
        }
    }
    return;
  }

  verify_frame = last_pop;
  last_pop = NULL;
  
  if (verify_frame->op != head) {
    PRINT("Error! Returned to op "PX" but expected op "PX"!\n", 
          (uint64) head, (uint64) verify_frame->op);
    return;
  }
  if (verify_frame->continuation_index != index) {
    PRINT("Error! Returned to index %d but expected index %d!\n",
          index, verify_frame->continuation_index);
    return;
  }
  
  PRINT("Verified return from %s to %s.%d\n", (uint64) last_context.name, 
        current_context.name, index);
}

/*
const char *get_current_interp_context_name()
{
  return current_context.name;
}

const uint get_current_interp_context_id()
{
  return current_context.id;
}

void set_staged_interp_context(const char *name, uint id)
{
  ASSERT(staged_context.name == NULL);
  staged_context.name = name;
  staged_context.id = id;
}
*/  
