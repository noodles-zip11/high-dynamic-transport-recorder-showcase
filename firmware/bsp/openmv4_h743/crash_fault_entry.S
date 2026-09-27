#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED

.syntax unified
.cpu cortex-m7
.thumb

.equ CRASH_RECORD_FAULT_HARDFAULT, 1
.equ CRASH_RECORD_FAULT_MEMMANAGE, 2
.equ CRASH_RECORD_FAULT_BUSFAULT, 3
.equ CRASH_RECORD_FAULT_USAGEFAULT, 4

.extern crash_record_fault_capture

.macro CRASH_FAULT_BODY kind
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    mov r1, lr                  /* Preserve EXC_RETURN unchanged. */
    movs r2, #\kind
    b crash_record_fault_capture
.endm

.global crash_record_HardFault_Handler
.type crash_record_HardFault_Handler, %function
crash_record_HardFault_Handler:
    CRASH_FAULT_BODY CRASH_RECORD_FAULT_HARDFAULT
.size crash_record_HardFault_Handler, .-crash_record_HardFault_Handler

.global crash_record_MemManage_Handler
.type crash_record_MemManage_Handler, %function
crash_record_MemManage_Handler:
    CRASH_FAULT_BODY CRASH_RECORD_FAULT_MEMMANAGE
.size crash_record_MemManage_Handler, .-crash_record_MemManage_Handler

.global crash_record_BusFault_Handler
.type crash_record_BusFault_Handler, %function
crash_record_BusFault_Handler:
    CRASH_FAULT_BODY CRASH_RECORD_FAULT_BUSFAULT
.size crash_record_BusFault_Handler, .-crash_record_BusFault_Handler

.global crash_record_UsageFault_Handler
.type crash_record_UsageFault_Handler, %function
crash_record_UsageFault_Handler:
    CRASH_FAULT_BODY CRASH_RECORD_FAULT_USAGEFAULT
.size crash_record_UsageFault_Handler, .-crash_record_UsageFault_Handler

#endif
