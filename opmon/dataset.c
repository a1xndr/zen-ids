#include "php.h"

#include "lib/script_cfi_utils.h"
#include "cfg.h"
#include "dataset.h"

#pragma pack(push, 4)

typedef struct _dataset_call_target_t {
  uint unit_hash;
  uint routine_hash;
} dataset_call_target_t;

typedef struct _dataset_call_targets_t {
  uint target_count;
  dataset_call_target_t targets[1];
} dataset_call_targets_t;

typedef struct _dataset_node_t {
  zend_uchar opcode;
  zend_uchar pad[3];
  union {
    uint zero; // [normal node]
    uint target_index; // [branch node]
    uint call_targets; // dataset_call_targets_t * [call node] (relative to dataset top)
    uint something; // [eval node]
  };
} dataset_node_t;

struct _dataset_routine_t {
  uint unit_hash;
  uint routine_hash;
  uint node_count;
  dataset_node_t nodes[1];
};

typedef struct _dataset_chain_t {
  uint routine; // dataset_routine_t * (relative to dataset top)
} dataset_chain_t;

typedef struct _dataset_hashtable_t {
  uint mask;
  uint table[1]; // dataset_chain_t * (relative to dataset top)
} dataset_hashtable_t;

typedef struct _dataset_eval_list_t {
  uint count;
  uint list[1];
} dataset_eval_list_t;

#pragma pack(pop)

static uint_ptr_t dataset_mapping = 0;
static dataset_hashtable_t *hashtable;
static dataset_eval_list_t *eval_list; 

#define RESOLVE_PTR(ptr, type) ((type *)(dataset_mapping + ((uint_ptr_t)(ptr) * 4)))

void install_dataset(void *dataset)
{
  dataset_mapping = (uint_ptr_t) dataset;
  hashtable = RESOLVE_PTR(*(uint *) dataset_mapping, dataset_hashtable_t);
  eval_list = (dataset_eval_list_t *)(hashtable->table + (hashtable->mask + 1));
}

uint dataset_get_eval_count()
{
  return eval_list->count;
}

dataset_routine_t *dataset_routine_lookup(uint unit_hash, uint routine_hash)
{
  if (dataset_mapping == 0) {
    return NULL;
  } else if (unit_hash == EVAL_HASH) {
    return RESOLVE_PTR(eval_list->list[routine_hash], dataset_routine_t);
  } else {
    uint index = (unit_hash ^ routine_hash) & hashtable->mask;
    dataset_routine_t *routine;
    dataset_chain_t *chain = RESOLVE_PTR(hashtable->table[index], dataset_chain_t);
    while (p2int(chain) != dataset_mapping) {
      routine = RESOLVE_PTR(chain->routine, dataset_routine_t);
      if (routine->unit_hash == unit_hash && routine->routine_hash == routine_hash)
        return routine;
      chain++;
    }
    return NULL;
  }
}

void dataset_match_eval(control_flow_metadata_t *cfm)
{
  uint i, j;
  dataset_routine_t *routine;
  bool match;
  
  if (eval_list == NULL)
    return;
  
  for (i = 0; i < eval_list->count; i++) {
    routine = RESOLVE_PTR(eval_list->list[i], dataset_routine_t);
    if (routine->node_count != cfm->cfg->opcode_count)
      continue;
    match = true;
    for (j = 0; j < routine->node_count; j++) {
      if (routine->nodes[j].opcode != cfm->cfg->opcodes[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      PRINT("<MON> Matched eval %d to dataset eval %d\n", cfm->cfg->routine_hash, i);
      
      cfm->dataset = routine;
      cfm->cfg->routine_hash = i;
      break;
    }
  }
  
  if (cfm->dataset == NULL)
    PRINT("<MON> Failed to match eval %d\n", cfm->cfg->routine_hash);
}

void dataset_routine_verify_compiled_edge(dataset_routine_t *dataset, 
                                          uint from_index, uint to_index)
{
  dataset_node_t *node = &dataset->nodes[from_index];
  if (to_index == (from_index + 1) || node->target_index == to_index) {
    PRINT("<MON> Verified compiled edge from %d to %d\n", from_index, to_index);
  } else {
    PRINT("<MON> Opcode edge mismatch at index %d: expected target %d but found target %d\n", 
          from_index, node->target_index, to_index);
  }
}

void dataset_routine_verify_opcode(dataset_routine_t *dataset, uint index, 
                                   zend_uchar opcode)
{
  dataset_node_t *node = &dataset->nodes[index];
  if (node->opcode == opcode) {
    PRINT("<MON> Verified opcode %d at index %d\n", opcode, index);
  } else {
    PRINT("<MON> Opcode mismatch at index %d: expected %d but found %d\n", index, 
          node->opcode, opcode);
  }
}

bool dataset_verify_routine_edge(dataset_routine_t *dataset, uint from_index, 
                                 uint to_unit_hash, uint to_routine_hash)
{
  uint i;
  dataset_node_t *node = &dataset->nodes[from_index];
  dataset_call_targets_t *targets = RESOLVE_PTR(node->call_targets, dataset_call_targets_t);
  
  for (i = 0; i < targets->target_count; i++) {
    if (targets->targets[i].unit_hash == to_unit_hash && targets->targets[i].routine_hash == to_routine_hash)
      return true;
  }
  return false;
}