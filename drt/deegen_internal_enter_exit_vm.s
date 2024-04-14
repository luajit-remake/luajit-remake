	.text
	.file	"deegen_internal_enter_exit_vm.c"
	
# deegen_enter_vm_from_c_impl
#   Enter the VM from C code.
#
	.globl	deegen_enter_vm_from_c_impl  
	.p2align	4, 0x90
	.type	deegen_enter_vm_from_c_impl,@function
deegen_enter_vm_from_c_impl:  
	# Input (Linux C calling convention):
	#   arg 0 (%rdi): CoroutineCtx
	#   arg 1 (%rsi): the GHC-calling-conv callee
	#   arg 2 (%rdx): stackBase
	#   arg 3 (%rcx): numArgs
	#   arg 4 (%r8): cb
	#
	# The GHC calling convention callee expects:
	#   dst 0 (%r13): CoroutineCtx
	#   dst 1 (%rbp): stackBase
	#   dst 2 (%r12): numArgs
	#   dst 3 (%rbx): cb
	#   dst 4 (%r14): tag register 1
	#   dst 5 (%rsi): (unused)
	#   dst 6 (%rdi): isMustTail64 (should be 0)
	#   dst 7 (%r8) : (unused)
	#   dst 8 (%r9) : (unused)
	#   dst 9 (%r15): tag register 2
	#
	# Push all the callee-saved registers in Linux C calling convention.
	# Note that we happen to push 48 bytes, and we are branching to the callee.
	# So the callee will see a 16-byte-aligned %rsp as expected. 
	#
	pushq	%rbp
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbx
	
	# Set up the registers expected by the GHC calling convention callee
	#
	
	# Move CoroutineCtx (%rdi)
	#
	movq	%rdi, %r13
	
	# Move stackBase (%rdx)
	#
	movq	%rdx, %rbp
	
	# Move numArgs (%rcx)
	#
	movq	%rcx, %r12
	
	# Move cb (%r8)
	#
	movq	%r8, %rbx
	
	# Set isMustTail64 (%rdi) to 0
	# Note that %rdi is originally holding CoroutineCtx, so this must be done after moving it
	#
	xor 	%edi, %edi
	
	# set up tag register 1 (%r14, x_int32Tag)
	#
	movabsq	$0xfffbffff00000000, %r14
	
	# set up tag register 2 (%r15, x_mivTag)
	#
	movabsq	$0xfffcffff0000007f, %r15
	
	# Branch to callee (%rsi)
	# The stack is unbalanced yet, but it's fine because by design control must 
	# eventually transfer to deegen_internal_use_only_exit_vm_epilogue, 
	# which will restore the callee-saved registers, re-balance the stack,
	# and return control to C
	#
	jmpq	*%rsi
	ud2
	
.Lfunc_end_deegen_enter_vm_from_c_impl:
	.size	deegen_enter_vm_from_c_impl, .Lfunc_end_deegen_enter_vm_from_c_impl-deegen_enter_vm_from_c_impl
          
# deegen_internal_use_only_exit_vm_epilogue
#   Clean up the stack and return control to C.
#   Never called directly from C. 
#
	.globl	deegen_internal_use_only_exit_vm_epilogue  
	.p2align	4, 0x90
	.type	deegen_internal_use_only_exit_vm_epilogue,@function
deegen_internal_use_only_exit_vm_epilogue:  
	# Input (GHC calling convention):
	#   arg 0 (%r13): CoroutineCtx (unused)
	#   arg 1 (%rbp): stackBase (unused)
	#   arg 2 (%r12): (unused)
	#   arg 3 (%rbx): (unused)
	#   arg 4 (%r14): tag register 1 (unused)
	#   arg 5 (%rsi): retStart
	#   arg 6 (%rdi): numRets
	#   arg 7 (%r8) : (unused)
	#   arg 8 (%r9) : (unused)
	#   arg 9 (%r15): tag register 2 (unused)
	#
	# Returns (Linux C calling convention, 'DeegenInternalEnterVMFromCReturnResults')
	#   %rax: retStart
	#   %rdx: numRets
	#
	
	# Clean up the stack and restore the callee-saved registers of C calling convention
	#
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	popq	%rbp
	
	# Set up the return values and return to C code
	#
	movq	%rsi, %rax
	movq	%rdi, %rdx
	retq
	
.Lfunc_end_deegen_internal_use_only_exit_vm_epilogue:
	.size	deegen_internal_use_only_exit_vm_epilogue, .Lfunc_end_deegen_internal_use_only_exit_vm_epilogue-deegen_internal_use_only_exit_vm_epilogue

	.section	".note.GNU-stack","",@progbits
	 
