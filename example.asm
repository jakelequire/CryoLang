	.def	@feat.00;
	.scl	3;
	.type	0;
	.endef
	.globl	@feat.00
.set @feat.00, 0
	.file	"Example"
	.def	"Example::Point::new";
	.scl	2;
	.type	32;
	.endef
	.text
	.globl	"Example::Point::new"
	.p2align	4
"Example::Point::new":
.seh_proc "Example::Point::new"
	subq	$24, %rsp
	.seh_stackalloc 24
	.seh_endprologue
	movl	%ecx, %eax
	movl	%ecx, 4(%rsp)
	movl	%edx, (%rsp)
	movl	%ecx, 8(%rsp)
	movl	%edx, 12(%rsp)
	movq	$0, 16(%rsp)
	xorps	%xmm0, %xmm0
	addq	$24, %rsp
	retq
	.seh_endproc

	.def	"Example::Point::square";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::Point::square"
	.p2align	4
"Example::Point::square":
.seh_proc "Example::Point::square"
	pushq	%rax
	.seh_stackalloc 8
	.seh_endprologue
	movq	%rcx, (%rsp)
	movl	(%rcx), %edx
	movl	4(%rcx), %eax
	imull	%edx, %edx
	imull	%eax, %eax
	addl	%edx, %eax
	popq	%rcx
	retq
	.seh_endproc

	.def	"Example::Point::print";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::Point::print"
	.p2align	4
"Example::Point::print":
.seh_proc "Example::Point::print"
	subq	$40, %rsp
	.seh_stackalloc 40
	.seh_endprologue
	movq	%rcx, 32(%rsp)
	movl	(%rcx), %edx
	movl	4(%rcx), %r8d
	movabsq	$.L.str, %rcx
	movabsq	$printf, %rax
	callq	*%rax
	nop
	addq	$40, %rsp
	retq
	.seh_endproc

	.def	"Example::print_color";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::print_color"
	.p2align	4
"Example::print_color":
	subq	$40, %rsp
	movl	%ecx, 36(%rsp)
	testl	%ecx, %ecx
	je	.LBB3_1
	cmpl	$1, %ecx
	jne	.LBB3_6
	movabsq	$.L.str.2, %rcx
	jmp	.LBB3_2
.LBB3_1:
	movabsq	$.L.str.1, %rcx
	jmp	.LBB3_2
.LBB3_6:
	cmpl	$2, %ecx
	jne	.LBB3_3
	movabsq	$.L.str.3, %rcx
.LBB3_2:
	movabsq	$printf, %rax
	callq	*%rax
.LBB3_3:
	addq	$40, %rsp
	retq

	.def	"Example::for_loop_example";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::for_loop_example"
	.p2align	4
"Example::for_loop_example":
	pushq	%rsi
	pushq	%rdi
	subq	$40, %rsp
	movl	$0, 36(%rsp)
	movabsq	$.L.str.4, %rsi
	movabsq	$printf, %rdi
	cmpl	$4, 36(%rsp)
	jg	.LBB4_3
	.p2align	4
.LBB4_2:
	movl	36(%rsp), %edx
	movq	%rsi, %rcx
	callq	*%rdi
	incl	36(%rsp)
	cmpl	$4, 36(%rsp)
	jle	.LBB4_2
.LBB4_3:
	addq	$40, %rsp
	popq	%rdi
	popq	%rsi
	retq

	.def	"Example::foo";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::foo"
	.p2align	4
"Example::foo":
	pushq	%rax
	andb	$1, %cl
	movb	%cl, 7(%rsp)
	je	.LBB5_2
	movl	$42, %eax
	popq	%rcx
	retq
.LBB5_2:
	xorl	%eax, %eax
	popq	%rcx
	retq

	.def	"Example::return_unit";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::return_unit"
	.p2align	4
"Example::return_unit":
	subq	$40, %rsp
	movabsq	$.L.str.5, %rcx
	movabsq	$printf, %rax
	callq	*%rax
	addq	$40, %rsp
	retq

	.def	main;
	.scl	2;
	.type	32;
	.endef
	.section	.rdata,"dr"
	.p2align	4, 0x0
.LCPI7_0:
	.long	1
	.long	2
	.long	3
	.long	4
	.text
	.globl	main
	.p2align	4
main:
	pushq	%rbp
	pushq	%rsi
	pushq	%rdi
	pushq	%rbx
	subq	$200, %rsp
	leaq	128(%rsp), %rbp
	movabsq	$__main, %rax
	callq	*%rax
	movabsq	$"Example::Point::new", %rax
	movl	$1, %ecx
	movl	$2, %edx
	callq	*%rax
	movl	%edx, %r8d
	movl	%eax, 48(%rbp)
	movl	%edx, 52(%rbp)
	movsd	%xmm0, 56(%rbp)
	leaq	52(%rbp), %rdi
	movabsq	$.L.str.6, %rcx
	movabsq	$printf, %rbx
	movl	%eax, %edx
	callq	*%rbx
	movabsq	$.L.str.7, %rcx
	callq	*%rbx
	movabsq	$.L.str.8, %rcx
	leaq	48(%rbp), %rsi
	movq	%rsi, %rdx
	callq	*%rbx
	movabsq	$.L.str.9, %rcx
	movq	%rsi, %rdx
	callq	*%rbx
	movabsq	$.L.str.10, %rcx
	movq	%rdi, %rdx
	callq	*%rbx
	movabsq	$.L.str.11, %rcx
	callq	*%rbx
	movabsq	$.L.str.12, %rcx
	movl	$16, %edx
	callq	*%rbx
	movabsq	$.L.str.13, %rcx
	movl	$8, %edx
	callq	*%rbx
	movabsq	$"Example::foo", %rax
	xorl	%ecx, %ecx
	callq	*%rax
	movl	%eax, 36(%rbp)
	movabsq	$.L.str.14, %rcx
	movl	%eax, %edx
	callq	*%rbx
	movabsq	$"Example::Point::square", %rax
	movq	%rsi, %rcx
	callq	*%rax
	movl	%eax, 40(%rbp)
	movabsq	$.L.str.15, %rcx
	movl	%eax, %edx
	callq	*%rbx
	xorps	%xmm0, %xmm0
	movaps	%xmm0, 48(%rbp)
	movabsq	$"Example::Point::print", %rax
	movq	%rsi, %rcx
	callq	*%rax
	movabsq	$"Example::return_unit", %rax
	callq	*%rax
	movabsq	$generic_fn_i32, %rax
	movl	$123, %ecx
	callq	*%rax
	movl	%eax, 44(%rbp)
	movabsq	$.L.str.16, %rcx
	movl	%eax, %edx
	callq	*%rbx
	movabsq	$.L.str.17, %rdx
	movabsq	$"Example::GenericStruct_string::new", %rax
	leaq	-24(%rbp), %rcx
	callq	*%rax
	movups	(%rbp), %xmm0
	movups	-16(%rbp), %xmm1
	movq	-24(%rbp), %rdx
	movq	%rdx, -88(%rbp)
	movups	%xmm1, -80(%rbp)
	movups	%xmm0, -64(%rbp)
	movabsq	$.L.str.18, %rcx
	callq	*%rbx
	movabsq	$.LCPI7_0, %rax
	movaps	(%rax), %xmm0
	movups	%xmm0, 16(%rbp)
	movl	$5, 32(%rbp)
	movq	32(%rbp), %rax
	movups	16(%rbp), %xmm0
	movups	%xmm0, -48(%rbp)
	movq	%rax, -32(%rbp)
	xorl	%eax, %eax
	addq	$200, %rsp
	popq	%rbx
	popq	%rdi
	popq	%rsi
	popq	%rbp
	retq

	.def	generic_fn_i32;
	.scl	2;
	.type	32;
	.endef
	.globl	generic_fn_i32
	.p2align	4
generic_fn_i32:
.seh_proc generic_fn_i32
	pushq	%rax
	.seh_stackalloc 8
	.seh_endprologue
	movl	%ecx, %eax
	movl	%ecx, 4(%rsp)
	popq	%rcx
	retq
	.seh_endproc

	.def	"Example::GenericStruct_string::new";
	.scl	2;
	.type	32;
	.endef
	.globl	"Example::GenericStruct_string::new"
	.p2align	4
"Example::GenericStruct_string::new":
.seh_proc "Example::GenericStruct_string::new"
	subq	$48, %rsp
	.seh_stackalloc 48
	.seh_endprologue
	movq	%rcx, %rax
	movq	%rdx, 40(%rsp)
	movq	%rdx, (%rsp)
	movq	%rdx, 8(%rsp)
	movq	%rdx, 16(%rsp)
	movq	%rdx, 24(%rsp)
	movq	%rdx, 32(%rsp)
	movups	(%rsp), %xmm0
	movups	16(%rsp), %xmm1
	movq	%rdx, 32(%rcx)
	movups	%xmm1, 16(%rcx)
	movups	%xmm0, (%rcx)
	addq	$48, %rsp
	retq
	.seh_endproc

	.section	.rdata,"dr"
	.globl	foobar
foobar:
	.byte	1

	.globl	barfoo
barfoo:
	.byte	0

	.globl	baz
	.p2align	2, 0x0
baz:
	.long	204235524

	.globl	neg
	.p2align	2, 0x0
neg:
	.long	4282732951

.L.str:
	.asciz	"(%d, %d)\n"

.L.str.1:
	.asciz	"Red\n"

.L.str.2:
	.asciz	"Green\n"

.L.str.3:
	.asciz	"Blue\n"

.L.str.4:
	.asciz	"i: %d\n"

.L.str.5:
	.asciz	"This function returns unit type.\n"

.L.str.6:
	.asciz	"Point: (%d, %d)\n"

.L.str.7:
	.asciz	"Addresses: \n"

.L.str.8:
	.asciz	"p:\t%p\n"

.L.str.9:
	.asciz	"p.x:\t%p\n"

.L.str.10:
	.asciz	"p.y:\t%p\n"

.L.str.11:
	.asciz	"Sizes: \n"

.L.str.12:
	.asciz	"sizeof(Point): %d\n"

.L.str.13:
	.asciz	"alignof(Point): %d\n"

.L.str.14:
	.asciz	"Result of foo(false): %d\n"

.L.str.15:
	.asciz	"Square of point: %d\n"

.L.str.16:
	.asciz	"Generic function result: %d\n"

.L.str.17:
	.asciz	"Hello, Cryo!"

.L.str.18:
	.asciz	"Generic struct value: %s\n"

