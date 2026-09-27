#ifndef TRANSPORT_RECORDER_DMA_BUFFER_H
#define TRANSPORT_RECORDER_DMA_BUFFER_H

#include "project_config.h"


#if defined(__GNUC__)
#define TRANSPORT_DMA_BUFFER \
    __attribute__((section(".dma_buffer"), aligned(TRANSPORT_DMA_ALIGNMENT_BYTES)))
#else
#error "TRANSPORT_DMA_BUFFER requires a compiler-specific section attribute"
#endif

#endif
