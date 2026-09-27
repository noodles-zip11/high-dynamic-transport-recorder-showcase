#include <stdio.h>

#include "health_service.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "health: %s\n", message);
        return 0;
    }
    return 1;
}

static health_inputs_t healthy_inputs(void)
{
    health_inputs_t inputs = {0};

    inputs.acquisition.ready = RT_TRUE;
    inputs.event.ready = RT_TRUE;
    inputs.storage.ready = RT_TRUE;
    inputs.acquisition.progress_sequence = 1U;
    inputs.event.progress_sequence = 1U;
    inputs.storage.progress_sequence = 1U;
    inputs.utc_valid = RT_TRUE;
    inputs.environment_valid = RT_TRUE;
    inputs.environment_fresh = RT_TRUE;
    return inputs;
}

static int test_stale_core_progress_becomes_fault_after_its_deadline(void)
{
    health_service_t service;
    health_inputs_t inputs = healthy_inputs();
    health_snapshot_t snapshot;

    health_service_init(&service);
    health_service_evaluate(&service, &inputs, 1000000U);
    health_service_get_snapshot(&service, &snapshot);
    if (!expect(service.snapshot_mutex.take_count == 2U
                && service.snapshot_mutex.release_count == 2U,
                "evaluate and snapshot copy must each hold the service mutex")
        || !expect(snapshot.state == HEALTH_HEALTHY,
                "initial ready providers with progress must be healthy"))
    {
        return 0;
    }

    health_service_evaluate(&service, &inputs, 4000001U);
    health_service_get_snapshot(&service, &snapshot);
    return expect(snapshot.state == HEALTH_FAULT,
                  "acquisition/event progress stale for more than 3 seconds must fault");
}

static int test_idle_ready_storage_does_not_become_stale(void)
{
    health_service_t service;
    health_inputs_t inputs = healthy_inputs();
    health_snapshot_t snapshot;

    inputs.storage_progress_required = RT_FALSE;
    health_service_init(&service);
    health_service_evaluate(&service, &inputs, 0U);
    inputs.acquisition.progress_sequence++;
    inputs.event.progress_sequence++;
    health_service_evaluate(&service, &inputs, 11000000U);
    health_service_get_snapshot(&service, &snapshot);
    return expect(snapshot.state == HEALTH_HEALTHY,
                  "an idle but ready storage service must not become stale");
}

static int test_provider_loss_after_start_is_fault_and_blocks_watchdog(void)
{
    health_service_t service;
    health_inputs_t inputs = healthy_inputs();
    health_snapshot_t snapshot;

    health_service_init(&service);
    health_service_evaluate(&service, &inputs, 0U);
    inputs.storage.ready = RT_FALSE;
    health_service_evaluate(&service, &inputs, 1000000U);
    health_service_get_snapshot(&service, &snapshot);
    return expect(snapshot.state == HEALTH_FAULT,
                  "a provider that fails after startup must fault, not restart")
        && expect(!health_service_watchdog_feed_allowed(&service),
                  "a faulted supervisor must deny watchdog feeding");
}

static int test_power_droop_degrades_and_sustained_backpressure_faults(void)
{
    health_service_t service;
    health_inputs_t inputs = healthy_inputs();
    health_snapshot_t snapshot;

    health_service_init(&service);
    health_service_evaluate(&service, &inputs, 0U);
    inputs.power_state = HEALTH_POWER_STATE_DROOP;
    inputs.acquisition.progress_sequence++;
    inputs.event.progress_sequence++;
    health_service_evaluate(&service, &inputs, 1000000U);
    health_service_get_snapshot(&service, &snapshot);
    if (!expect(snapshot.state == HEALTH_DEGRADED,
                "power droop must degrade a recording-ready system"))
    {
        return 0;
    }

    inputs.power_state = HEALTH_POWER_STATE_NORMAL;
    inputs.sample_pool_backpressure_count = 1U;
    inputs.acquisition.progress_sequence++;
    inputs.event.progress_sequence++;
    health_service_evaluate(&service, &inputs, 2000000U);
    inputs.sample_pool_backpressure_count = 2U;
    inputs.acquisition.progress_sequence++;
    inputs.event.progress_sequence++;
    health_service_evaluate(&service, &inputs, 5000001U);
    health_service_get_snapshot(&service, &snapshot);
    return expect(snapshot.state == HEALTH_FAULT,
                  "backpressure rising for three seconds must fault");
}

int main(void)
{
    if (!test_stale_core_progress_becomes_fault_after_its_deadline()
        || !test_idle_ready_storage_does_not_become_stale()
        || !test_provider_loss_after_start_is_fault_and_blocks_watchdog()
        || !test_power_droop_degrades_and_sustained_backpressure_faults())
    {
        return 1;
    }
    puts("health service: PASS");
    return 0;
}
