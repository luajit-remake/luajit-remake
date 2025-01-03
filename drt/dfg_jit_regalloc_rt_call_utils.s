	.text
	.file	"dfg_jit_regalloc_rt_call_utils.c"
	
###
# See deegen/deegen_dfg_jit_regalloc_rt_call_wrapper.h for design
###

	.globl	deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue  
	.p2align	4, 0x90
	.type	deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue,@function
deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue:  

	movq %r11, 8(%rsp)
	movups %xmm1, 16(%rsp)
	movups %xmm2, 32(%rsp)
	movups %xmm3, 48(%rsp)
	movups %xmm4, 64(%rsp)
	movups %xmm5, 80(%rsp)
	movups %xmm6, 96(%rsp)
	ret
	
.Lfunc_end_deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue:
	.size	deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue, .Lfunc_end_deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue-deegen_dfg_jit_regalloc_preserve_most_rt_call_prologue
          

	.globl	deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue  
	.p2align	4, 0x90
	.type	deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue,@function
deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue:  
	
	movq 8(%rsp), %r11
	movups 16(%rsp), %xmm1  
	movups 32(%rsp), %xmm2  
	movups 48(%rsp), %xmm3
	movups 64(%rsp), %xmm4
	movups 80(%rsp), %xmm5 
	movups 96(%rsp), %xmm6  
	ret
	
.Lfunc_end_deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue:
	.size	deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue, .Lfunc_end_deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue-deegen_dfg_jit_regalloc_preserve_most_rt_call_epilogue

	.section	".note.GNU-stack","",@progbits
	 
