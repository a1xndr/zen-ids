#include "php_opcode_monitor.h"
#include "interp_context.h"
#include "compile_context.h"
#include "cfg_handler.h"
#include "cfg.h"
#include "dataflow.h"
#include "taint.h"
#include "operand_resolver.h"
#include "lib/script_cfi_utils.h"
#include "lib/script_cfi_array.h"
#include "event_handler.h"

#define MAX_FUNCTION_NAME 256
#define MAX_STACK_FRAME 256
#define ENV_STATIC_ANALYSIS "OPMON_STATIC_ANALYSIS"

#define CHECK_FUNCTION_NAME_LENGTH(len) \
do { \
  if ((len) >= MAX_FUNCTION_NAME) \
    ERROR("function_name exceeds max length 256!\n"); \
} while (0)

typedef enum _opmon_run_type {
  OPMON_RUN_EXECUTION,
  OPMON_RUN_STATIC_ANALYSIS,
  OPMON_RUN_DATAFLOW_ANALYSIS
} opmon_run_type;

static const char *static_analysis = NULL;
static opmon_run_type run_type = OPMON_RUN_EXECUTION;
zend_dataflow_t *dataflow_stack_base = NULL;
zend_dataflow_monitor_t *dataflow_hooks = NULL;
static zend_opcode_monitor_t *opcode_hooks = NULL;

#define DATAFLOW_STACK_ENTRY_COUNT 0x2000
#define DATAFLOW_STACK_SIZE (sizeof(zend_dataflow_t) * DATAFLOW_STACK_ENTRY_COUNT)

// #define DEBUG_NOP 1
// #define DEBUG_PHASES 1

static void init_top_level_script(const char *script_path)
{
  starting_script(script_path);
}

static void init_worker()
{
  worker_startup();
}

void request_boundary(bool is_request_start)
{
  uint64 request_id = interp_request_boundary(is_request_start);
  cfg_request_boundary(is_request_start, request_id);

  if (is_request_start) {
    metadata_start_request();
  } else {
    scfree_request();
    taint_clear();
  }
}

const char *get_static_analysis()
{
  return static_analysis;
}

bool is_static_analysis()
{
  return run_type == OPMON_RUN_STATIC_ANALYSIS;
}

static int start_dataflow_analysis(zend_file_handle *file)
{
  if (run_type != OPMON_RUN_EXECUTION) {
    ERROR("Cannot perform static analysis and dataflow analysis at the same time!\n");
    return FAILURE;
  }

  run_type = OPMON_RUN_DATAFLOW_ANALYSIS;
  return analyze_dataflow(NULL, file);
}

bool is_dataflow_analysis()
{
  return run_type == OPMON_RUN_DATAFLOW_ANALYSIS;
}

static zend_bool zval_has_taint(const zval *value)
{
  return taint_var_get(value) != NULL;
}

/************************ NOP handlers ************************/

void nop_request_boundary(bool is_request_start)
{
}

zend_bool nop_notify_dataflow(const zval *src, const zval *dst, zend_bool is_internal_transfer)
{
  return false;
}

zend_bool nop_has_taint(const zval *value)
{
  return false;
}

void nop_notify_function_created(zend_op *src, zend_op_array *dst)
{
}

void nop_notify_zval_free(const zval *zv)
{
}

monitor_query_flags_t nop_notify_database_query(const char *query)
{
  return 0;
}

void nop_notify_database_fetch(uint32_t field_count, const char **table_names,
                                      const char **column_names, const zval **value)
{
}

void init_event_handler()
{
  // scarray_unit_test();

  dataflow_hooks = get_dataflow_monitor();
  dataflow_stack_base = PROCESS_ALLOC(DATAFLOW_STACK_SIZE);
  dataflow_hooks->dataflow_stack = dataflow_stack_base;
  memset(dataflow_hooks->dataflow_stack, 0, DATAFLOW_STACK_SIZE);

  opcode_hooks = get_opcode_monitor();

  if (IS_CFI_DB() && !IS_REQUEST_ID_SYNCH_DB()) {
    ERROR("The cfi mode is incompatible with the request id synch!\n");
    return;
  }
  if (IS_CFI_FILE() && !IS_REQUEST_ID_SYNCH_FILE()) {
    ERROR("The cfi mode is incompatible with the request id synch!\n");
    return;
  }

  static_analysis = getenv(ENV_STATIC_ANALYSIS);
  SPOT("static_analysis: %s\n", static_analysis);
  if (static_analysis != NULL)
    run_type = OPMON_RUN_STATIC_ANALYSIS;

  init_utils();
  init_compile_context();
  init_cfg_handler();
  init_metadata_handler();
  init_taint_tracker();

  if (IS_OPCODE_DUMP_ENABLED())
    init_dataflow_analysis();

  opcode_hooks->set_top_level_script = init_top_level_script;
  opcode_hooks->notify_worker_startup = init_worker;
  opcode_hooks->opmon_tokenize = NULL; //tokenize_file;
  opcode_hooks->opmon_dataflow = start_dataflow_analysis;
  opcode_hooks->notify_database_query = db_query; // taint: always enabled

  /* always nop to begin--enabled (if ever) below in set_monitor_mode() */
  set_monitor_mode(MONITOR_MODE_NONE);

#if defined(DEBUG_NOP)
  opcode_hooks->notify_http_request = nop_request_boundary;
  opcode_hooks->notify_function_created = nop_notify_function_created;
  opcode_hooks->vm_call = vm_call_plain;
#elif defined(DEBUG_PHASES)
  opcode_hooks->notify_http_request = request_boundary;
  opcode_hooks->notify_function_created = function_created;
  opcode_hooks->vm_call = vm_call_plain;

  opcode_hooks->has_taint = nop_has_taint;
  opcode_hooks->notify_database_fetch = nop_notify_database_fetch;
  opcode_hooks->notify_database_query = nop_notify_database_query;
#else
  opcode_hooks->notify_http_request = request_boundary;
  opcode_hooks->notify_function_created = function_created;
  opcode_hooks->vm_call = vm_monitor_call_quick;
#endif

  if (IS_CFI_TRAINING()) {
    zend_execute_ex = execute_opcode_monitor_all;
    opcode_hooks->vm_call = vm_monitor_call;
    dataflow_hooks->is_training = true;
  }

  SPOT("SAPI type: %s\n", EG(sapi_type));

  if (strcmp(EG(sapi_type), "apache2handler") == 0)
    server_startup();
}

void set_monitor_mode(monitor_mode_t mode)
{
#if defined(DEBUG_NOP)
  mode = MONITOR_MODE_NONE;
#elif defined(DEBUG_PHASES)
  mode = MONITOR_MODE_NONE;
#endif

  switch (mode) {
    case MONITOR_MODE_NONE:
      zend_execute_ex = execute_opcode_direct;

      opcode_hooks->vm_call = vm_call_plain;

      dataflow_hooks->is_enabled = false;

      SPOT("set_monitor_mode(MONITOR_MODE_NONE)\n");
      break;
    case MONITOR_MODE_CALLS:
      zend_execute_ex = execute_opcode_monitor_calls;

      opcode_hooks->vm_call = vm_monitor_call_quick;
      opcode_hooks->has_taint = nop_has_taint;
      opcode_hooks->notify_database_fetch = nop_notify_database_fetch;

      dataflow_hooks->is_enabled = false;

      SPOT("set_monitor_mode(MONITOR_MODE_CALLS)\n");
      break;
    case MONITOR_MODE_ALL:
      zend_execute_ex = execute_opcode_monitor_all;

      opcode_hooks->vm_call = vm_monitor_call;
      opcode_hooks->has_taint = zval_has_taint;
      opcode_hooks->notify_database_fetch = db_fetch_trigger;

      dataflow_hooks->is_enabled = true;

      SPOT("set_monitor_mode(MONITOR_MODE_ALL)\n");
      break;
  }

  reset_dataflow_stack();
}

void reset_dataflow_stack()
{
  if (dataflow_hooks->dataflow_stack > dataflow_stack_base) {
    memset(dataflow_stack_base, 0, (char *) dataflow_hooks->dataflow_stack - (char *) dataflow_stack_base);
    dataflow_hooks->dataflow_stack = dataflow_stack_base;
  }
}

void destroy_event_handler()
{
  destroy_utils();
  destroy_metadata_handler();
  destroy_operand_resolver();
  destroy_cfg_handler();
  destroy_taint_tracker();

  if (run_type == OPMON_RUN_DATAFLOW_ANALYSIS)
    destroy_dataflow_analysis();
}
