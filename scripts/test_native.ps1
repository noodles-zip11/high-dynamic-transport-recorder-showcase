$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

$native = Join-Path $ProjectRoot 'firmware\tests\native'

& $SConsExe -C $native -j4 -Q

Push-Location $ProjectRoot
try {
foreach ($name in @(
    'native_smoke.exe',
    'test_memory_layout.exe',
    'test_power_policy.exe',
    'test_power_status_format.exe',
    'test_board_power_openmv4.exe',
    'test_board_power_weact.exe',
    'test_power_runtime.exe',
    'test_system_report_format.exe',
    'test_icm45686.exe',
    'test_icm45686_fifo.exe',
    'test_imu_acquisition.exe',
    'test_imu_dma_power_guard.exe',
    'test_imu_sample_batcher.exe',
    'test_imu_acquisition_stats.exe',
    'test_sample_block_pool.exe',
    'test_trigger_detector.exe',
    'test_pretrigger_ring.exe',
    'test_event_assembler.exe',
    'test_event_export_debug.exe',
    'test_event_quality.exe',
    'test_event_quality_service.exe',
    'test_reliability_evidence.exe',
    'test_crash_record.exe',
    'test_storage_format_confirmation.exe',
    'test_storage_service_format.exe',
    'test_event_service.exe',
    'test_fake_nor.exe',
    'test_nor_flash_w25q.exe',
    'test_event_log.exe',
    'test_sht4x.exe',
    'test_environment_service.exe',
    'test_time_service.exe',
    'test_monotonic_clock.exe',
    'test_reset_power_service.exe',
    'test_health_service.exe',
    'test_terp.exe',
    'test_terp_device.exe',
    'test_terp_reliability.exe',
    'test_terp_service.exe',
    'test_terp_service_reliability.exe',
    'test_ota_package.exe',
    'test_ota_state.exe',
    'test_ota_slots.exe',
    'test_ota_model_package.exe',
    'test_ota_model_lifecycle.exe',
    'test_ai_features.exe',
    'test_ai_runtime.exe',
    'test_ai_model_data.exe',
    'test_ai_golden_vectors.exe',
    'test_ai_result_sidecar.exe',
    'test_ai_inference_service.exe',
    'test_ai_inference_queue_pressure.exe',
    'test_ai_inference_queue_pressure_arm.exe'
)) {
    $exe = Join-Path $native "build\$name"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Native test executable not found: $exe"
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Output 'Native C tests: PASS'
}
finally {
    Pop-Location
}
