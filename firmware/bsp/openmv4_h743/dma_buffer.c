#include <stdint.h>

#include "dma_buffer.h"


/* Link-time probe only. No peripheral DMA is enabled in Phase 03. */
TRANSPORT_DMA_BUFFER uint8_t transport_dma_layout_probe[32];

_Static_assert(__alignof__(transport_dma_layout_probe) ==
                   TRANSPORT_DMA_ALIGNMENT_BYTES,
               "DMA probe must be cache-line aligned");
