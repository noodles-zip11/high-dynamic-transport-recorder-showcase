#ifndef TRANSPORT_RECORDER_OTA_QSPI_CANDIDATE_H
#define TRANSPORT_RECORDER_OTA_QSPI_CANDIDATE_H

#include <stdbool.h>
#include <stdint.h>

bool ota_qspi_candidate_init(void);
bool ota_qspi_bus_lock(void);
void ota_qspi_bus_unlock(void);
int ota_qspi_candidate_erase(void *context, uint32_t offset, uint32_t length);
int ota_qspi_candidate_write(void *context, uint32_t offset,
                             const uint8_t *data, uint32_t length);
int ota_qspi_candidate_read(void *context, uint32_t offset,
                            uint8_t *data, uint32_t length);
int ota_qspi_recovery_erase(void *context, uint32_t offset, uint32_t length);
int ota_qspi_recovery_write(void *context, uint32_t offset,
                            const uint8_t *data, uint32_t length);
int ota_qspi_recovery_read(void *context, uint32_t offset,
                           uint8_t *data, uint32_t length);
int ota_qspi_ai_result_erase(void *context, uint32_t offset, uint32_t length);
int ota_qspi_ai_result_write(void *context, uint32_t offset,
                             const uint8_t *data, uint32_t length);
int ota_qspi_ai_result_read(void *context, uint32_t offset,
                            uint8_t *data, uint32_t length);
int ota_qspi_model_a_erase(void *context, uint32_t offset, uint32_t length);
int ota_qspi_model_a_write(void *context, uint32_t offset,
                           const uint8_t *data, uint32_t length);
int ota_qspi_model_a_read(void *context, uint32_t offset,
                          uint8_t *data, uint32_t length);
int ota_qspi_model_b_erase(void *context, uint32_t offset, uint32_t length);
int ota_qspi_model_b_write(void *context, uint32_t offset,
                           const uint8_t *data, uint32_t length);
int ota_qspi_model_b_read(void *context, uint32_t offset,
                          uint8_t *data, uint32_t length);
int ota_qspi_model_state_erase(void *context, uint32_t offset, uint32_t length);
int ota_qspi_model_state_write(void *context, uint32_t offset,
                               const uint8_t *data, uint32_t length);
int ota_qspi_model_state_read(void *context, uint32_t offset,
                              uint8_t *data, uint32_t length);
int ota_qspi_model_state_bank_erase(void *context, uint8_t bank,
                                    uint32_t offset, uint32_t length);
int ota_qspi_model_state_bank_write(void *context, uint8_t bank,
                                    uint32_t offset, const uint8_t *data,
                                    uint32_t length);
int ota_qspi_model_state_bank_read(void *context, uint8_t bank,
                                   uint32_t offset, uint8_t *data,
                                   uint32_t length);

#endif
