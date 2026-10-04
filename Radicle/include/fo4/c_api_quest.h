#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Quest runtime rows.
 *
 * radicle_quest_runtime_rows decodes one QUST record's VMAD and DNAM through Radicle's
 * QUST/VMAD reader and returns one tab-separated row per fact. Rows are released with
 * radicle_free_string_array from fo4/c_api.h, and the same contract applies: a row is a
 * NUL-terminated string owned by the caller.
 *
 *   quest <form key> <start game enabled 0|1>
 *   script <script name>
 *   script-property <script name> <property name> <property type code>
 *   fragment-script <script name>
 *   fragment-property <property name> <property type code>
 *   fragment <stage> <stage index> <script> <function>
 *   alias <alias id>
 *   alias-script <alias id> <script name>
 *   alias-script-property <alias id> <script name> <property name> <type code>
 *   unsupported <reason>
 *
 * An `unsupported` row is the explicit report the evidence rules require: the reader never
 * guesses, so anything it could not decode is counted in the row set instead of being
 * silently dropped. It is not an error status. A missing quest, or a form key that is not a
 * QUST, produces an `unsupported` row as well.
 *
 * `form_key` is the same "000800:Plugin.esp" text radicle_plugin_collect_form_keys returns.
 */
RADICLE_API radicle_status radicle_quest_runtime_rows(const radicle_plugin* plugin,
                                                       const char* form_key,
                                                       char*** out_rows,
                                                       size_t* out_count);

#ifdef __cplusplus
}
#endif
