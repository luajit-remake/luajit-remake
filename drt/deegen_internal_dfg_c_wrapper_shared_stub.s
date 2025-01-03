	.text
	.file	"deegen_internal_dfg_c_wrapper_shared_stub.c"
	
	.globl	__deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub  
	.p2align	4, 0x90
	.type	__deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub,@function
__deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub:  

	# preserve_most does not save %r11, but we need to save r11 as well (and make preserve_all save r11) so it can work with reg alloc
	#
	movq %r11, 8(%rsp)
	
	# save all the FPR regs that participates in DFG reg alloc
	# Our caller stubs will save any additional regs used by the code
	#
	movups %xmm1, 16(%rsp)
	movups %xmm2, 32(%rsp)
	movups %xmm3, 48(%rsp)
	movups %xmm4, 64(%rsp)
	movups %xmm5, 80(%rsp)
	movups %xmm6, 96(%rsp)
	retq

.Lfunc_end___deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub:
	.size	__deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub, .Lfunc_end___deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub-__deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub
          
	.globl	__deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub  
	.p2align	4, 0x90
	.type	__deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub,@function
__deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub:  

	# Restore everything saved by the prologue stub above
	#
	movq 8(%rsp), %r11
	movups 16(%rsp), %xmm1
	movups 32(%rsp), %xmm2
	movups 48(%rsp), %xmm3
	movups 64(%rsp), %xmm4
	movups 80(%rsp), %xmm5
	movups 96(%rsp), %xmm6
	retq
	
.Lfunc_end___deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub:
	.size	__deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub, .Lfunc_end___deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub-__deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub
	
	.section	".note.GNU-stack","",@progbits

