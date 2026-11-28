#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_condition {
  uint8_t flags_byte;
  uint32_t comparison_raw;
  uint16_t function;
  uint16_t unknown2;
  int32_t param1;
  int32_t param2;
  uint32_t run_on;
  uint32_t reference;
  int32_t unknown3;
  const char* parameter1_string;
  const char* parameter2_string;
} radicle_condition;

typedef struct radicle_condition_list {
  const radicle_condition* rows;
  size_t count;
  void* internal;
} radicle_condition_list;

typedef enum radicle_condition_write_status {
  RADICLE_CONDITION_UPDATED = 0,
  RADICLE_CONDITION_UNCHANGED = 1,
  RADICLE_CONDITION_NO_LIST = 2,
  RADICLE_CONDITION_MULTIPLE_LISTS = 3,
  RADICLE_CONDITION_ALIAS_STRUCTURE_INVALID = 4,
  RADICLE_CONDITION_SCOPE_INDEX_OUT_OF_RANGE = 5,
  RADICLE_CONDITION_ELEMENT_NOT_CONDITIONED = 6,
  RADICLE_CONDITION_UNREPRESENTABLE = 7
} radicle_condition_write_status;

RADICLE_API radicle_status radicle_record_conditions(const radicle_env* env,
                                                     const radicle_record* record,
                                                     const char* scope_path,
                                                     const radicle_condition_list** out_list);
RADICLE_API void radicle_condition_list_free(const radicle_condition_list* list);
RADICLE_API radicle_status radicle_plugin_set_conditions(
    radicle_plugin* plugin, radicle_form_key record, const char* scope_path,
    const radicle_condition* rows, size_t count, radicle_condition_write_status* out_status);

// Constraint: parameter_names are the schema's snake_case parameter-type names in declared order; slots past parameter_count are NULL and editor is 1 for rows the schema flags for editors.
typedef struct radicle_condition_function_meta {
  uint16_t index;
  const char* name;
  uint8_t parameter_count;
  const char* parameter_names[3];
  uint8_t editor;
} radicle_condition_function_meta;

typedef struct radicle_condition_run_on_meta {
  uint32_t value;
  const char* name;
} radicle_condition_run_on_meta;

typedef struct radicle_condition_schema_meta {
  const radicle_condition_function_meta* functions;
  size_t function_count;
  const radicle_condition_run_on_meta* run_ons;
  size_t run_on_count;
} radicle_condition_schema_meta;

// Constraint: the schema table is compiled in, so no environment or plugin handle is required and the result must be released with radicle_condition_schema_free.
RADICLE_API radicle_status radicle_condition_schema_get(
    const radicle_condition_schema_meta** out_schema);
RADICLE_API void radicle_condition_schema_free(
    const radicle_condition_schema_meta* schema);

typedef enum radicle_condition_verdict {
  RADICLE_CONDITION_VERDICT_FALSE = 0,
  RADICLE_CONDITION_VERDICT_TRUE = 1,
  RADICLE_CONDITION_VERDICT_UNEVALUABLE = 2
} radicle_condition_verdict;

typedef struct radicle_condition_value {
  int32_t known;
  double value;
  int32_t global_known;
  double global_value;
  uint32_t resolved_run_on;
  uint32_t resolved_reference;
} radicle_condition_value;

typedef struct radicle_condition_result {
  radicle_condition_verdict verdict;
  size_t first_unevaluable_row;
  uint16_t first_unevaluable_function;
  size_t rows_evaluated;
} radicle_condition_result;

RADICLE_API radicle_status radicle_conditions_evaluate(const radicle_condition* rows,
                                                       const radicle_condition_value* values,
                                                       size_t count, radicle_condition_result* out);
/* Constraint: name is matched ASCII case-insensitively against the CTDA function table; an unknown name is NOT_FOUND. */
RADICLE_API radicle_status radicle_condition_function_id(const char* name, uint16_t* out_id);

typedef struct radicle_condition_function_facts {
  const char* name;
  uint8_t takes_form_param;
  uint8_t first_param_is_quest;
} radicle_condition_function_facts;

/* Constraint: name has static lifetime; takes_form_param is set when the first parameter is a form link and first_param_is_quest when it is a quest link, both from the CTDA function table; an unknown id is NOT_FOUND. */
RADICLE_API radicle_status radicle_condition_function_info(uint16_t id, radicle_condition_function_facts* out);

typedef enum radicle_condition_param_kind {
  RADICLE_CONDITION_PARAM_KIND_NONE = 0,
  RADICLE_CONDITION_PARAM_KIND_INTEGER = 1,
  RADICLE_CONDITION_PARAM_KIND_FLOAT = 2,
  RADICLE_CONDITION_PARAM_KIND_STRING = 3,
  RADICLE_CONDITION_PARAM_KIND_FORM = 4,
  RADICLE_CONDITION_PARAM_KIND_ENUM = 5,
  RADICLE_CONDITION_PARAM_KIND_OTHER = 6,
  RADICLE_CONDITION_PARAM_KIND_INDEX = 7
} radicle_condition_param_kind;

/* Constraint: type_name is the schema's static snake_case parameter type (NULL for NONE); number is the signed raw value for INTEGER, ENUM, OTHER and INDEX and the float value for FLOAT; text points into the input row's string for STRING; form is a resolved key for FORM, with form.plugin NULL and form_unresolved 0 for a null link and form_unresolved 1 for an id outside the record plugin's masters. */
typedef struct radicle_condition_param {
  radicle_condition_param_kind kind;
  int32_t raw;
  double number;
  const char* type_name;
  const char* text;
  radicle_form_key form;
  int32_t form_unresolved;
} radicle_condition_param;

/* Constraint: function_name has static lifetime and is NULL when the function id is not in the table, in which case both parameters are NONE; comparison is FLOAT, or FORM of type global when the use-global flag is set; reference is FORM of type reference for run-on Reference and otherwise typed from the run-on. */
typedef struct radicle_condition_typed {
  const char* function_name;
  radicle_condition_param param1;
  radicle_condition_param param2;
  radicle_condition_param comparison;
  radicle_condition_param reference;
} radicle_condition_typed;

/* Constraint: out must hold count entries, entry i types rows[i], and record must be the record the rows were read from because its master table resolves the FormIDs; a bad FormID never fails the call, it only sets form_unresolved on that slot. Strings in out borrow from rows and keys from env. A form-link slot that the aliases or package-data flag switched reads as INDEX, not FORM. */
RADICLE_API radicle_status radicle_condition_params(const radicle_env* env,
                                                    const radicle_record* record,
                                                    const radicle_condition* rows, size_t count,
                                                    radicle_condition_typed* out);

#ifdef __cplusplus
}
#endif
