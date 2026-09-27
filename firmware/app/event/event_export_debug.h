#ifndef TRANSPORT_RECORDER_EVENT_EXPORT_DEBUG_H
#define TRANSPORT_RECORDER_EVENT_EXPORT_DEBUG_H

#include <stdint.h>

#include "event_assembler.h"
#include "event_export_format.h"

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#include "event_quality.h"
#endif

typedef rt_err_t (*event_export_write_fn)(const uint8_t *data,
                                          rt_size_t length,
                                          void *context);

rt_err_t event_export_debug_write(event_assembler_t *assembler,
                                  sample_block_pool_t *pool,
                                  event_export_write_fn write,
                                  void *context);

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
rt_err_t event_export_debug_decode_header(const uint8_t *data,
                                          uint32_t length,
                                          uint32_t ev01_length,
                                          event_quality_facts_t *facts);
#endif

#endif
