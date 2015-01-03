#include "php.h"
#include "php_opcode_monitor.h"
#include "lib/script_cfi_utils.h"
#include "metadata_handler.h"
#include "cfg_handler.h"
#include "interp_context.h"

typedef struct _interp_context_t {
  const char *name;
  uint id;
  control_flow_metadata_t cfm;
} interp_context_t;

typedef struct _shadow_frame_t {
  zend_op *op;
  uint continuation_index;
  interp_context_t context;
} shadow_frame_t;

static cfg_t *app_cfg;

static shadow_frame_t shadow_stack[256];
static shadow_frame_t *shadow_frame;
static shadow_frame_t *last_pop;

static interp_context_t current_context = { "<root>", 0, NULL };
static interp_context_t last_context = { NULL, 0, NULL };

#define CONTEXT_ENTRY 0xff
static const cfg_node_t context_entry_node = { CONTEXT_ENTRY, 0xffffffff };
static cfg_node_t last_node;

extern cfg_files_t cfg_files;

static uint get_last_branch_index()
{
  return (shadow_frame - 1)->continuation_index - 1;
}

static void app_cfg_add_edge(control_flow_metadata_t *from_cfm, routine_cfg_t *to_cfg, cfg_node_t from_node)
{
  bool write_edge = true;
  cfg_add_routine(app_cfg, to_cfg);
  cfg_add_routine_edge(app_cfg, from_node, from_cfm->cfg, to_cfg);
  
  if (from_cfm->dataset != NULL) {
    if (dataset_verify_routine_edge(from_cfm->dataset, from_node.index, 
                                    to_cfg->unit_hash, to_cfg->routine_hash)) {
      write_edge = false;
      PRINT("<MON> Verified routine edge [0x%x|0x%x|%d -> 0x%x|0x%x]\n", 
            from_cfm->cfg->unit_hash,
            from_cfm->cfg->routine_hash, from_node.index, 
            to_cfg->unit_hash, to_cfg->routine_hash);
    }
  }
  
  if (write_edge) {
    PRINT("<MON> New routine edge [0x%x|0x%x|%d -> 0x%x|0x%x]\n", 
          from_cfm->cfg->unit_hash, 
          from_cfm->cfg->routine_hash, from_node.index, 
          to_cfg->unit_hash, to_cfg->routine_hash);
    write_routine_edge(from_cfm->cfg->unit_hash, from_cfm->cfg->routine_hash, from_node.index, 
                       to_cfg->unit_hash, to_cfg->routine_hash, 0 /* durf */);
  }
}

void initialize_interp_context()
{
  app_cfg = cfg_new();
  shadow_frame = shadow_stack;
  shadow_frame->op = NULL;
  last_pop = NULL;
  last_node = context_entry_node;
}

void push_interp_context(zend_op* op, uint branch_index, control_flow_metadata_t cfm)
{
  if (cfm.cfg != NULL && current_context.cfm.cfg != NULL) {
    cfg_node_t from_node = { current_context.cfm.cfg->opcodes[branch_index], branch_index };
    
    PRINT("# Push interp context 0x%x|0x%x\n", cfm.cfg->unit_hash, cfm.cfg->routine_hash);
    
    app_cfg_add_edge(&current_context.cfm, cfm.cfg, from_node);
  }
  
  shadow_frame->op = op;
  shadow_frame->continuation_index = branch_index + 1;
  shadow_frame->context = current_context;
  
  shadow_frame++;
  last_context = current_context;
  current_context.cfm = cfm;
  
  last_node = context_entry_node;
}

void set_interp_cfm(control_flow_metadata_t cfm)
{
  uint branch_index = get_last_branch_index();
  
  PRINT("# Set interp context 0x%x|0x%x\n", cfm.cfg->unit_hash, cfm.cfg->routine_hash);
  
  if (last_context.cfm.cfg != NULL) {
    cfg_node_t from_node = { last_context.cfm.cfg->opcodes[branch_index], branch_index };
    
    PRINT("# Adding edge from last context\n");
    
    app_cfg_add_edge(&last_context.cfm, cfm.cfg, from_node);
  } else {
    PRINT("[skip routine edge because the last context has no cfg]\n");
  }
  
  current_context.cfm = cfm;
}

// in general, do popped objects need to be freed?
void pop_interp_context()
{
  last_pop = --shadow_frame;
  last_context = current_context;
  current_context = shadow_frame->context;
  
  if (last_context.cfm.cfg != NULL) {
    PRINT("# Pop interp context to null\n");
  } else {
    PRINT("# Pop interp context to 0x%x|0x%x\n", 
          current_context.cfm.cfg->unit_hash, current_context.cfm.cfg->routine_hash);
  }
  
  last_node = context_entry_node;
}

void verify_interp_context(zend_op* head, cfg_node_t node)
{
  shadow_frame_t *verify_frame;
  cfg_node_t from_node = last_node;
  
  last_node = node;
  
  if (last_pop == NULL) {
    if ((from_node.opcode != CONTEXT_ENTRY) && (node.index != (from_node.index + 1))) {
      bool found = false;
      uint i;
      for (i = 0; i < current_context.cfm.cfg->edge_count; i++) {
        cfg_opcode_edge_t *edge = &current_context.cfm.cfg->edges[i];
        if (edge->from_index == from_node.index && edge->to_index == node.index) {
          found = true;
          break;
        }
      }
      if (!found) 
        PRINT("Error! Expected index %d but found %d\n", from_node.index + 1, node.index);
    }
    
    /*
    switch (node.opcode) {
      case ZEND_INCLUDE_OR_EVAL:
      case ZEND_DECLARE_FUNCTION:
      case ZEND_DECLARE_LAMBDA_FUNCTION:
      case ZEND_RECV:
        PRINT("Executing a disappearing opcode: %s at %d in [0x%x|0x%x]\n", 
              zend_get_opcode_name(node.opcode), node.index, 
              current_context.cfm.cfg->unit_hash, current_context.cfm.cfg->routine_hash);
        break;
      default:
        if (node.opcode != current_context.cfm.cfg->opcodes[node.index]) {
          PRINT("Error! Expected opcode %s at index %d, but found opcode %s\n", 
                zend_get_opcode_name(current_context.cfm.cfg->opcodes[node.index]), node.index,
                zend_get_opcode_name(node.opcode));
        }
    }
    */
    if (node.opcode != current_context.cfm.cfg->opcodes[node.index]) {
      PRINT("Error! Expected opcode %s at index %d, but found opcode %s\n", 
            zend_get_opcode_name(current_context.cfm.cfg->opcodes[node.index]), node.index,
            zend_get_opcode_name(node.opcode));
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
  if (verify_frame->continuation_index != node.index) {
    PRINT("Error! Returned to index %d but expected index %d!\n",
          node.index, verify_frame->continuation_index);
    return;
  }
  
  PRINT("Verified return from %s to %s.%d\n", last_context.name, 
        current_context.name, node.index);
}
