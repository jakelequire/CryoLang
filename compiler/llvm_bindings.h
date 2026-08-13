/*
 * llvm_bindings.h - Thin LLVM-C API wrapper for the Cryo compiler.
 *
 * Declares only the LLVM-C functions needed by cryoc's codegen phase.
 * All LLVM handles are typedef'd to void* for clean FFI across the
 * Cryo/C boundary.  The actual symbols are resolved at link time by
 * linking against libLLVM.
 *
 * To add a new LLVM-C function: add its declaration here, then access
 * it from Cryo as  llvm::FunctionName(...).
 */

#ifndef CRYO_LLVM_BINDINGS_H
#define CRYO_LLVM_BINDINGS_H

/* size_t / uint64_t used below come from these. Required now that the C-import
 * engine (libclang) semantically PARSES this header rather than only
 * preprocessing it: without the declarations clang would default the unknown
 * type names to implicit-int (i32), silently truncating the 64-bit DIBuilder
 * size/length arguments. (Preprocess-only scanning never resolved types, so
 * the spellings `size_t`/`uint64_t` mapped to u64 by name - this keeps both
 * engines in agreement; verified parity-neutral for the old scanner.) */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ===================================================================
 * Opaque handle types - all void* on the Cryo side
 * =================================================================== */

typedef void *LLVMModuleRef;
typedef void *LLVMBuilderRef;
typedef void *LLVMValueRef;
typedef void *LLVMTypeRef;
typedef void *LLVMBasicBlockRef;
typedef void *LLVMContextRef;
typedef void *LLVMTargetRef;
typedef void *LLVMTargetMachineRef;
typedef void *LLVMTargetDataRef;
typedef void *LLVMPassManagerRef;
typedef void *LLVMPassBuilderOptionsRef;
typedef void *LLVMMemoryBufferRef;
typedef void *LLVMAttributeRef;
typedef void *LLVMMetadataRef;
typedef void *LLVMDIBuilderRef;

typedef int LLVMBool;


/* ===================================================================
 * Context
 * =================================================================== */

LLVMContextRef LLVMContextCreate(void);
LLVMContextRef LLVMGetGlobalContext(void);
void           LLVMContextDispose(LLVMContextRef C);


/* ===================================================================
 * Module
 * =================================================================== */

LLVMModuleRef  LLVMModuleCreateWithName(const char *ModuleID);
LLVMModuleRef  LLVMModuleCreateWithNameInContext(const char *ModuleID, LLVMContextRef C);
void           LLVMDisposeModule(LLVMModuleRef M);
void           LLVMSetTarget(LLVMModuleRef M, const char *Triple);
void           LLVMSetDataLayout(LLVMModuleRef M, const char *DataLayoutStr);
const char    *LLVMGetDataLayoutStr(LLVMModuleRef M);
char          *LLVMPrintModuleToString(LLVMModuleRef M);
LLVMBool       LLVMPrintModuleToFile(LLVMModuleRef M, const char *Filename, char **ErrorMessage);
LLVMBool       LLVMVerifyModule(LLVMModuleRef M, int Action, char **OutMessage);
/* Bitcode serialization for the multi-process object emitter: the main process
 * writes each module's bitcode, child processes parse it back and emit .o. */
int            LLVMWriteBitcodeToFile(LLVMModuleRef M, const char *Path);
LLVMBool       LLVMCreateMemoryBufferWithContentsOfFile(const char *Path, LLVMMemoryBufferRef *OutMemBuf, char **OutMessage);
/* LLVMParseIRInContext parses textual OR bitcode IR and TAKES OWNERSHIP of
 * MemBuf (do not dispose it afterwards). */
LLVMBool       LLVMParseIRInContext(LLVMContextRef ContextRef, LLVMMemoryBufferRef MemBuf, LLVMModuleRef *OutM, char **OutMessage);
void           LLVMDumpModule(LLVMModuleRef M);
LLVMContextRef LLVMGetModuleContext(LLVMModuleRef M);
LLVMValueRef   LLVMGetFirstFunction(LLVMModuleRef M);
LLVMValueRef   LLVMGetNextFunction(LLVMValueRef Fn);
LLVMTypeRef    LLVMGlobalGetValueType(LLVMValueRef Global);
const char    *LLVMGetValueName(LLVMValueRef Val);
void           LLVMGetParamTypes(LLVMTypeRef FunctionTy, LLVMTypeRef *Dest);
int            LLVMGetInstructionOpcode(LLVMValueRef Inst);
LLVMTypeRef    LLVMGetAllocatedType(LLVMValueRef Alloca);
LLVMValueRef   LLVMGetOperand(LLVMValueRef Val, unsigned Index);
int            LLVMGetNumOperands(LLVMValueRef Val);
LLVMValueRef   LLVMGetFirstInstruction(LLVMBasicBlockRef BB);
LLVMValueRef   LLVMGetNextInstruction(LLVMValueRef Inst);


/* ===================================================================
 * Types - Integer
 * =================================================================== */

LLVMTypeRef LLVMInt1TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt8TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt16TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt32TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt64TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt128TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMIntTypeInContext(LLVMContextRef C, unsigned NumBits);

LLVMTypeRef LLVMInt1Type(void);
LLVMTypeRef LLVMInt8Type(void);
LLVMTypeRef LLVMInt16Type(void);
LLVMTypeRef LLVMInt32Type(void);
LLVMTypeRef LLVMInt64Type(void);
LLVMTypeRef LLVMInt128Type(void);
LLVMTypeRef LLVMIntType(unsigned NumBits);
unsigned    LLVMGetIntTypeWidth(LLVMTypeRef IntegerTy);


/* ===================================================================
 * Types - Floating Point
 * =================================================================== */

LLVMTypeRef LLVMFloatTypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMDoubleTypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMFloatType(void);
LLVMTypeRef LLVMDoubleType(void);


/* ===================================================================
 * Types - Void / Label / Pointer
 * =================================================================== */

LLVMTypeRef LLVMVoidTypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMVoidType(void);
int LLVMGetTypeKind(LLVMTypeRef Ty);  /* 0 = LLVMVoidTypeKind */
/* Context-audit introspection: which LLVMContext owns a type, and a printable
 * form.  Used by the codegen context auditor to detect a type that leaked into
 * the global context (would be shared across per-module contexts). */
LLVMContextRef LLVMGetTypeContext(LLVMTypeRef Ty);
char          *LLVMPrintTypeToString(LLVMTypeRef Ty);
LLVMTypeRef LLVMPointerType(LLVMTypeRef ElementType, unsigned AddressSpace);
LLVMTypeRef LLVMPointerTypeInContext(LLVMContextRef C, unsigned AddressSpace);


/* ===================================================================
 * Types - Function
 * =================================================================== */

LLVMTypeRef LLVMFunctionType(LLVMTypeRef ReturnType, LLVMTypeRef *ParamTypes,
                             unsigned ParamCount, LLVMBool IsVarArg);
unsigned    LLVMCountParamTypes(LLVMTypeRef FunctionTy);
LLVMBool    LLVMIsFunctionVarArg(LLVMTypeRef FunctionTy);
LLVMTypeRef LLVMGetReturnType(LLVMTypeRef FunctionTy);


/* ===================================================================
 * Types - Struct
 * =================================================================== */

LLVMTypeRef LLVMStructTypeInContext(LLVMContextRef C, LLVMTypeRef *ElementTypes,
                                    unsigned ElementCount, LLVMBool Packed);
LLVMTypeRef LLVMStructCreateNamed(LLVMContextRef C, const char *Name);
LLVMTypeRef LLVMGetTypeByName2(LLVMContextRef C, const char *Name);
void        LLVMStructSetBody(LLVMTypeRef StructTy, LLVMTypeRef *ElementTypes,
                              unsigned ElementCount, LLVMBool Packed);
unsigned    LLVMCountStructElementTypes(LLVMTypeRef StructTy);
LLVMTypeRef LLVMStructGetTypeAtIndex(LLVMTypeRef StructTy, unsigned i);
LLVMBool    LLVMIsOpaqueStruct(LLVMTypeRef StructTy);


/* ===================================================================
 * Types - Array
 * =================================================================== */

LLVMTypeRef LLVMArrayType(LLVMTypeRef ElementType, unsigned ElementCount);
LLVMTypeRef LLVMArrayType2(LLVMTypeRef ElementType, unsigned long long ElementCount);
unsigned    LLVMGetArrayLength(LLVMTypeRef ArrayTy);
LLVMTypeRef LLVMGetElementType(LLVMTypeRef Ty);


/* ===================================================================
 * Types - Vector
 * ===================================================================
 *
 * SysV x86-64 packs two `float` fields that share an eightbyte into a
 * single `<2 x float>` SSE register slot.  The classifier emits this
 * type for multi-float SSE buckets via `LType::vector_of(f32, 2)`.
 */

LLVMTypeRef LLVMVectorType(LLVMTypeRef ElementType, unsigned ElementCount);


/* ===================================================================
 * Values - General
 * =================================================================== */

LLVMTypeRef    LLVMTypeOf(LLVMValueRef Val);
const char    *LLVMGetValueName2(LLVMValueRef Val, unsigned long *Length);
void           LLVMSetValueName2(LLVMValueRef Val, const char *Name, unsigned long NameLen);
void           LLVMDumpValue(LLVMValueRef Val);
char          *LLVMPrintValueToString(LLVMValueRef Val);
void           LLVMSetLinkage(LLVMValueRef Global, int Linkage);
int            LLVMGetLinkage(LLVMValueRef Global);
/* Object-file section placement (drives `![section("name")]`). */
void           LLVMSetSection(LLVMValueRef Global, const char *Section);

/* COMDAT - required for linkonce_odr / weak_odr to dedupe on COFF.
 * On ELF the linker auto-dedupes by section group; COFF requires an
 * explicit comdat group per symbol.  LLVM does NOT auto-attach a
 * comdat for linkonce_odr on COFF - callers must call
 * LLVMSetComdat(fn, LLVMGetOrInsertComdat(mod, name)).
 *
 * Selection kind defaults to LLVMAnyComdatSelectionKind (0), which is
 * the right pick for linkonce_odr: "pick any, all definitions are
 * equivalent". */
typedef void *LLVMComdatRef;
LLVMComdatRef  LLVMGetOrInsertComdat(LLVMModuleRef M, const char *Name);
void           LLVMSetComdat(LLVMValueRef V, LLVMComdatRef C);
LLVMComdatRef  LLVMGetComdat(LLVMValueRef V);
int            LLVMGetComdatSelectionKind(LLVMComdatRef C);
void           LLVMSetComdatSelectionKind(LLVMComdatRef C, int Kind);


/* ===================================================================
 * Constants
 * =================================================================== */

LLVMValueRef LLVMConstInt(LLVMTypeRef IntTy, unsigned long long N, LLVMBool SignExtend);
/* Build an integer constant from its textual digits in the given radix.
 * Needed for i128/u128 literals whose value exceeds 64 bits (LLVMConstInt
 * takes only a 64-bit N). Text must be the bare digits (no sign/prefix). */
LLVMValueRef LLVMConstIntOfString(LLVMTypeRef IntTy, const char *Text, unsigned char Radix);
LLVMValueRef LLVMConstReal(LLVMTypeRef RealTy, double N);
LLVMValueRef LLVMConstNull(LLVMTypeRef Ty);
LLVMBool LLVMIsNull(LLVMValueRef Val);
LLVMBool LLVMIsConstant(LLVMValueRef Val);
unsigned long long LLVMConstIntGetZExtValue(LLVMValueRef ConstantVal);
LLVMValueRef LLVMConstAllOnes(LLVMTypeRef Ty);
LLVMValueRef LLVMGetUndef(LLVMTypeRef Ty);
LLVMValueRef LLVMConstPointerNull(LLVMTypeRef Ty);
LLVMValueRef LLVMConstString(const char *Str, unsigned Length, LLVMBool DontNullTerminate);
LLVMValueRef LLVMConstStringInContext(LLVMContextRef C, const char *Str,
                                      unsigned Length, LLVMBool DontNullTerminate);
LLVMValueRef LLVMConstStruct(LLVMValueRef *ConstantVals, unsigned Count, LLVMBool Packed);
LLVMValueRef LLVMConstStructInContext(LLVMContextRef C, LLVMValueRef *ConstantVals,
                                      unsigned Count, LLVMBool Packed);
LLVMValueRef LLVMConstNamedStruct(LLVMTypeRef StructTy, LLVMValueRef *ConstantVals,
                                  unsigned Count);
LLVMValueRef LLVMConstArray(LLVMTypeRef ElementTy, LLVMValueRef *ConstantVals,
                            unsigned Length);
LLVMValueRef LLVMConstArray2(LLVMTypeRef ElementTy, LLVMValueRef *ConstantVals,
                             unsigned long long Length);
LLVMValueRef LLVMSizeOf(LLVMTypeRef Ty);
LLVMValueRef LLVMAlignOf(LLVMTypeRef Ty);
LLVMValueRef LLVMConstBitCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstIntToPtr(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstPtrToInt(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstGEP2(LLVMTypeRef Ty, LLVMValueRef ConstantVal,
                            LLVMValueRef *ConstantIndices, unsigned NumIndices);


/* ===================================================================
 * Global Variables
 * =================================================================== */

LLVMValueRef LLVMAddGlobal(LLVMModuleRef M, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMGetNamedGlobal(LLVMModuleRef M, const char *Name);
LLVMValueRef LLVMGetFirstGlobal(LLVMModuleRef M);
LLVMValueRef LLVMGetNextGlobal(LLVMValueRef GlobalVar);
LLVMValueRef LLVMGetInitializer(LLVMValueRef GlobalVar);
void         LLVMSetInitializer(LLVMValueRef GlobalVar, LLVMValueRef ConstantVal);
void         LLVMSetGlobalConstant(LLVMValueRef GlobalVar, LLVMBool IsConstant);
void         LLVMSetThreadLocal(LLVMValueRef GlobalVar, LLVMBool IsThreadLocal);
void         LLVMSetUnnamedAddress(LLVMValueRef Global, int UnnamedAddr);


/* ===================================================================
 * Functions
 * =================================================================== */

LLVMValueRef    LLVMAddFunction(LLVMModuleRef M, const char *Name, LLVMTypeRef FunctionTy);
LLVMValueRef    LLVMGetNamedFunction(LLVMModuleRef M, const char *Name);
unsigned        LLVMCountParams(LLVMValueRef Fn);
LLVMValueRef    LLVMGetParam(LLVMValueRef Fn, unsigned Index);
void            LLVMSetFunctionCallConv(LLVMValueRef Fn, unsigned CC);
LLVMBasicBlockRef LLVMGetEntryBasicBlock(LLVMValueRef Fn);
LLVMBasicBlockRef LLVMGetFirstBasicBlock(LLVMValueRef Fn);
LLVMBasicBlockRef LLVMGetLastBasicBlock(LLVMValueRef Fn);
LLVMBasicBlockRef LLVMGetNextBasicBlock(LLVMBasicBlockRef BB);
unsigned          LLVMCountBasicBlocks(LLVMValueRef Fn);


/* ===================================================================
 * Basic Blocks
 * =================================================================== */

LLVMBasicBlockRef LLVMAppendBasicBlockInContext(LLVMContextRef C, LLVMValueRef Fn,
                                                const char *Name);
LLVMBasicBlockRef LLVMAppendBasicBlock(LLVMValueRef Fn, const char *Name);
LLVMBasicBlockRef LLVMInsertBasicBlock(LLVMBasicBlockRef InsertBeforeBB, const char *Name);
void              LLVMDeleteBasicBlock(LLVMBasicBlockRef BB);
LLVMValueRef      LLVMGetBasicBlockTerminator(LLVMBasicBlockRef BB);
LLVMValueRef      LLVMGetBasicBlockParent(LLVMBasicBlockRef BB);
LLVMValueRef      LLVMBasicBlockAsValue(LLVMBasicBlockRef BB);

/* Use list - `LLVMGetFirstUse` returns NULL when a value has no uses.
 * For a basic-block-as-value that means no branch/blockaddress targets
 * it, i.e. the block has no predecessors (dead / unreachable). */
typedef void *LLVMUseRef;
LLVMUseRef        LLVMGetFirstUse(LLVMValueRef Val);


/* ===================================================================
 * Builder
 * =================================================================== */

LLVMBuilderRef LLVMCreateBuilder(void);
LLVMBuilderRef LLVMCreateBuilderInContext(LLVMContextRef C);
void           LLVMDisposeBuilder(LLVMBuilderRef Builder);
void           LLVMPositionBuilderAtEnd(LLVMBuilderRef Builder, LLVMBasicBlockRef Block);
void           LLVMPositionBuilderBefore(LLVMBuilderRef Builder, LLVMValueRef Instr);
LLVMBasicBlockRef LLVMGetInsertBlock(LLVMBuilderRef Builder);


/* ===================================================================
 * Builder - Terminators
 * =================================================================== */

LLVMValueRef LLVMBuildRetVoid(LLVMBuilderRef B);
LLVMValueRef LLVMBuildRet(LLVMBuilderRef B, LLVMValueRef V);
LLVMValueRef LLVMBuildBr(LLVMBuilderRef B, LLVMBasicBlockRef Dest);
LLVMValueRef LLVMBuildCondBr(LLVMBuilderRef B, LLVMValueRef If,
                             LLVMBasicBlockRef Then, LLVMBasicBlockRef Else);
LLVMValueRef LLVMBuildSwitch(LLVMBuilderRef B, LLVMValueRef V,
                             LLVMBasicBlockRef Else, unsigned NumCases);
void         LLVMAddCase(LLVMValueRef Switch, LLVMValueRef OnVal,
                         LLVMBasicBlockRef Dest);
LLVMValueRef LLVMBuildUnreachable(LLVMBuilderRef B);

/* ===================================================================
 * Builder - Atomic Operations
 *
 * Memory ordering values (LLVMAtomicOrdering, llvm-c/Core.h):
 *   0 = NotAtomic, 1 = Unordered, 2 = Monotonic (Relaxed),
 *   4 = Acquire, 5 = Release, 6 = AcquireRelease,
 *   7 = SequentiallyConsistent
 *
 * AtomicRMW binary-op values (LLVMAtomicRMWBinOp):
 *   0 = Xchg, 1 = Add, 2 = Sub, 3 = And, 4 = Nand,
 *   5 = Or, 6 = Xor, 7 = Max, 8 = Min, 9 = UMax, 10 = UMin
 *   (additional FP and inc/dec variants exist past 10; bind as int)
 *
 * Pass `int SingleThread = 0` for a normal multi-thread op (the only
 * thing the stdlib currently uses).  Per LLVM rules, atomic loads
 * and stores REQUIRE an explicit alignment - callers must invoke
 * LLVMSetAlignment after LLVMBuildLoad2/LLVMBuildStore for atomics.
 * =================================================================== */

/* Atomic fence - used by IntrinsicsCodegen to lower atomic_fence(order). */
LLVMValueRef LLVMBuildFence(LLVMBuilderRef B, int Ordering, int SingleThread,
                            const char *Name);

/* Atomic read-modify-write.  No Name parameter in LLVM-C. */
LLVMValueRef LLVMBuildAtomicRMW(LLVMBuilderRef B, int Op,
                                LLVMValueRef Ptr, LLVMValueRef Val,
                                int Ordering, int SingleThread);

/* Atomic compare-and-swap.  Returns the LLVM `{ T, i1 }` aggregate;
 * caller extracts index 0 (loaded value) and index 1 (success flag)
 * via LLVMBuildExtractValue.  No Name parameter in LLVM-C. */
LLVMValueRef LLVMBuildAtomicCmpXchg(LLVMBuilderRef B, LLVMValueRef Ptr,
                                    LLVMValueRef Cmp, LLVMValueRef New,
                                    int SuccessOrdering,
                                    int FailureOrdering,
                                    int SingleThread);

/* Set ordering on an existing load/store instruction.  Used to turn a
 * plain LLVMBuildLoad2 / LLVMBuildStore into an atomic load/store. */
void LLVMSetOrdering(LLVMValueRef MemoryAccessInst, int Ordering);

/* Set explicit alignment (in bytes) on a load/store/alloca/global.
 * Required for atomic loads and stores. */
void LLVMSetAlignment(LLVMValueRef V, unsigned Bytes);

/* Read the alignment (in bytes) a load/store/alloca/global carries.  An
 * alloca is born holding the ABI alignment of its LLVM type, so this reports
 * what LLVM guarantees unaided - the floor a caller raising alignment for an
 * `![align(N)]` type must not drop below. */
unsigned LLVMGetAlignment(LLVMValueRef V);


/* ===================================================================
 * Builder - Arithmetic
 * =================================================================== */

LLVMValueRef LLVMBuildAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildNSWAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildFAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildNSWSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildFSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildNSWMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildFMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildUDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildSDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildFDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildURem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildSRem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildFRem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildNeg(LLVMBuilderRef B, LLVMValueRef V, const char *Name);
LLVMValueRef LLVMBuildFNeg(LLVMBuilderRef B, LLVMValueRef V, const char *Name);


/* ===================================================================
 * Builder - Bitwise
 * =================================================================== */

LLVMValueRef LLVMBuildShl(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildLShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildAShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildAnd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildOr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildXor(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildNot(LLVMBuilderRef B, LLVMValueRef V, const char *Name);


/* ===================================================================
 * Builder - Comparisons
 * =================================================================== */

/* Integer comparison predicates */
enum {
    LLVMIntEQ  = 32, LLVMIntNE  = 33,
    LLVMIntUGT = 34, LLVMIntUGE = 35, LLVMIntULT = 36, LLVMIntULE = 37,
    LLVMIntSGT = 38, LLVMIntSGE = 39, LLVMIntSLT = 40, LLVMIntSLE = 41
};

/* Float comparison predicates */
enum {
    LLVMRealOEQ = 1, LLVMRealOGT = 2, LLVMRealOGE = 3,
    LLVMRealOLT = 4, LLVMRealOLE = 5, LLVMRealONE = 6,
    LLVMRealUNO = 8
};

LLVMValueRef LLVMBuildICmp(LLVMBuilderRef B, int Op, LLVMValueRef LHS,
                           LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildFCmp(LLVMBuilderRef B, int Op, LLVMValueRef LHS,
                           LLVMValueRef RHS, const char *Name);


/* ===================================================================
 * Builder - Memory
 * =================================================================== */

LLVMValueRef LLVMBuildAlloca(LLVMBuilderRef B, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMBuildLoad2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef PointerVal,
                            const char *Name);
LLVMValueRef LLVMBuildVAArg(LLVMBuilderRef B, LLVMValueRef List, LLVMTypeRef Ty,
                            const char *Name);
LLVMValueRef LLVMBuildStore(LLVMBuilderRef B, LLVMValueRef Val, LLVMValueRef Ptr);
LLVMValueRef LLVMBuildGEP2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Pointer,
                           LLVMValueRef *Indices, unsigned NumIndices, const char *Name);
LLVMValueRef LLVMBuildStructGEP2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Pointer,
                                 unsigned Idx, const char *Name);
LLVMValueRef LLVMBuildGlobalStringPtr(LLVMBuilderRef B, const char *Str, const char *Name);
LLVMValueRef LLVMBuildMemSet(LLVMBuilderRef B, LLVMValueRef Ptr, LLVMValueRef Val,
                             LLVMValueRef Len, unsigned Align);
LLVMValueRef LLVMBuildMemCpy(LLVMBuilderRef B, LLVMValueRef Dst, unsigned DstAlign,
                             LLVMValueRef Src, unsigned SrcAlign, LLVMValueRef Size);


/* ===================================================================
 * Builder - Casts
 * =================================================================== */

LLVMValueRef LLVMBuildTrunc(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildZExt(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildSExt(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPToUI(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPToSI(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildUIToFP(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildSIToFP(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPTrunc(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPExt(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildPtrToInt(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildIntToPtr(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildBitCast(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildPointerCast(LLVMBuilderRef B, LLVMValueRef Val, LLVMTypeRef DestTy, const char *Name);


/* ===================================================================
 * Builder - Other
 * =================================================================== */

LLVMValueRef LLVMBuildPhi(LLVMBuilderRef B, LLVMTypeRef Ty, const char *Name);
void         LLVMAddIncoming(LLVMValueRef PhiNode, LLVMValueRef *IncomingValues,
                             LLVMBasicBlockRef *IncomingBlocks, unsigned Count);
LLVMValueRef LLVMBuildCall2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Fn,
                            LLVMValueRef *Args, unsigned NumArgs, const char *Name);
LLVMValueRef LLVMBuildSelect(LLVMBuilderRef B, LLVMValueRef If,
                             LLVMValueRef Then, LLVMValueRef Else, const char *Name);
LLVMValueRef LLVMBuildExtractValue(LLVMBuilderRef B, LLVMValueRef AggVal,
                                   unsigned Index, const char *Name);
LLVMValueRef LLVMBuildInsertValue(LLVMBuilderRef B, LLVMValueRef AggVal,
                                  LLVMValueRef EltVal, unsigned Index, const char *Name);


/* ===================================================================
 * Exception handling (DWARF unwinding)
 *
 * `invoke` terminates a block with a normal edge (`Then`) and an unwind
 * edge (`Catch`, which must start with a `landingpad`).  The landing pad's
 * result type is the `{ ptr, i32 }` exception/selector pair; `PersFn` is
 * legacy and ignored by modern LLVM (the personality lives on the function,
 * set via LLVMSetPersonalityFn) - pass null.  A `cleanup` landing pad runs
 * destructors then `resume`s; a `catch` clause (LLVMAddClause with a type
 * info value, or a null pointer for catch-all) stops the unwind.
 * =================================================================== */

LLVMValueRef LLVMBuildInvoke2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Fn,
                              LLVMValueRef *Args, unsigned NumArgs,
                              LLVMBasicBlockRef Then, LLVMBasicBlockRef Catch,
                              const char *Name);
LLVMValueRef LLVMBuildLandingPad(LLVMBuilderRef B, LLVMTypeRef Ty,
                                 LLVMValueRef PersFn, unsigned NumClauses,
                                 const char *Name);
LLVMValueRef LLVMBuildResume(LLVMBuilderRef B, LLVMValueRef Exn);
void         LLVMAddClause(LLVMValueRef LandingPad, LLVMValueRef ClauseVal);
void         LLVMSetCleanup(LLVMValueRef LandingPad, LLVMBool Val);
void         LLVMSetPersonalityFn(LLVMValueRef Fn, LLVMValueRef PersonalityFn);


/* ===================================================================
 * Inline Assembly
 *
 * `Dialect` is really `LLVMInlineAsmDialect` (0 = ATT, 1 = Intel) but is
 * declared as `unsigned` here so the `![functions_only]` C-import doesn't
 * need to pull the enum type into the `llvm::` namespace.  The returned
 * value is used as the callee of LLVMBuildCall2.
 * =================================================================== */

LLVMValueRef LLVMGetInlineAsm(LLVMTypeRef Ty,
                              const char *AsmString, size_t AsmStringSize,
                              const char *Constraints, size_t ConstraintsSize,
                              LLVMBool HasSideEffects, LLVMBool IsAlignStack,
                              unsigned Dialect, LLVMBool CanThrow);
void LLVMAppendModuleInlineAsm(LLVMModuleRef M, const char *Asm, size_t Len);


/* ===================================================================
 * Target
 * =================================================================== */

void     LLVMInitializeX86TargetInfo(void);
void     LLVMInitializeX86Target(void);
void     LLVMInitializeX86TargetMC(void);
void     LLVMInitializeX86AsmPrinter(void);
void     LLVMInitializeX86AsmParser(void);

char    *LLVMGetDefaultTargetTriple(void);
LLVMBool LLVMGetTargetFromTriple(const char *Triple, LLVMTargetRef *T, char **ErrorMessage);

LLVMTargetMachineRef LLVMCreateTargetMachine(LLVMTargetRef T, const char *Triple,
                                             const char *CPU, const char *Features,
                                             int Level, int Reloc, int CodeModel);
void                 LLVMDisposeTargetMachine(LLVMTargetMachineRef T);
LLVMTargetDataRef    LLVMCreateTargetDataLayout(LLVMTargetMachineRef T);
char                *LLVMCopyStringRepOfTargetData(LLVMTargetDataRef TD);
void                 LLVMDisposeTargetData(LLVMTargetDataRef TD);

/* Module-owned data layout + type sizing (no dispose: owned by module) */
LLVMTargetDataRef    LLVMGetModuleDataLayout(LLVMModuleRef M);
unsigned long long   LLVMABISizeOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);
unsigned             LLVMABIAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);
unsigned long long   LLVMOffsetOfElement(LLVMTargetDataRef TD, LLVMTypeRef StructTy, unsigned Element);

/* Emit object/assembly to file */
LLVMBool LLVMTargetMachineEmitToFile(LLVMTargetMachineRef T, LLVMModuleRef M,
                                     const char *Filename, int codegen, char **ErrorMessage);

/* Codegen file types */
enum {
    LLVMAssemblyFile = 0,
    LLVMObjectFile   = 1
};

/* Optimization levels */
enum {
    LLVMCodeGenLevelNone       = 0,
    LLVMCodeGenLevelLess       = 1,
    LLVMCodeGenLevelDefault    = 2,
    LLVMCodeGenLevelAggressive = 3
};

/* Relocation models */
enum {
    LLVMRelocDefault        = 0,
    LLVMRelocStatic         = 1,
    LLVMRelocPIC            = 2,
    LLVMRelocDynamicNoPic   = 3
};

/* Code models */
enum {
    LLVMCodeModelDefault    = 0,
    LLVMCodeModelJITDefault = 1,
    LLVMCodeModelTiny       = 2,
    LLVMCodeModelSmall      = 3,
    LLVMCodeModelKernel     = 4,
    LLVMCodeModelMedium     = 5,
    LLVMCodeModelLarge      = 6
};

/* Linkage types */
enum {
    LLVMExternalLinkage            = 0,
    LLVMAvailableExternallyLinkage = 1,
    LLVMLinkOnceAnyLinkage         = 2,
    LLVMLinkOnceODRLinkage         = 3,
    LLVMWeakAnyLinkage             = 5,
    LLVMWeakODRLinkage             = 6,
    LLVMInternalLinkage            = 8,
    LLVMPrivateLinkage             = 9,
    LLVMExternalWeakLinkage        = 12
};


/* ===================================================================
 * Attributes (used for sret / byval ABI lowering)
 * ===================================================================
 *
 * `LLVMGetEnumAttributeKindForName` returns the numeric kind ID for a
 * named enum attribute ("sret", "byval", "noundef", ...).  The kind ID
 * is stable for the lifetime of the process and is the input to
 * `LLVMCreateEnumAttribute` / `LLVMCreateTypeAttribute`.
 *
 * `LLVMCreateTypeAttribute` builds a type-carrying attribute used for
 * `sret(%T)` / `byval(%T)` in modern LLVM (the typed-attribute form
 * was made mandatory in LLVM 15+ when opaque pointers landed).
 *
 * Attribute index conventions:
 *   * 0 (LLVMAttributeReturnIndex) - the function's return value
 *   * -1 / UINT_MAX (LLVMAttributeFunctionIndex) - the function itself
 *   * 1..N - parameter slot N (1-based)
 */

unsigned          LLVMGetEnumAttributeKindForName(const char *Name, unsigned long SLen);
LLVMAttributeRef  LLVMCreateEnumAttribute(LLVMContextRef C, unsigned KindID,
                                          unsigned long long Val);
LLVMAttributeRef  LLVMCreateTypeAttribute(LLVMContextRef C, unsigned KindID,
                                          LLVMTypeRef type_ref);
/* String ("target-dependent") attribute, e.g. "frame-pointer"="all".
 * Both key and value are explicit-length strings. */
LLVMAttributeRef  LLVMCreateStringAttribute(LLVMContextRef C,
                                            const char *K, unsigned KLength,
                                            const char *V, unsigned VLength);
void              LLVMAddAttributeAtIndex(LLVMValueRef F, unsigned Idx,
                                          LLVMAttributeRef A);
void              LLVMAddCallSiteAttribute(LLVMValueRef C, unsigned Idx,
                                           LLVMAttributeRef A);

/* Query for the presence of an enum-or-type attribute at a given
 * index.  Returns NULL when the attribute is not present.  Used by the
 * call-site marshaller to detect that a callee was declared with
 * `sret(%T)` so it can allocate a result slot and forward the hidden
 * pointer.  `LLVMGetTypeAttributeValue` extracts the pointee type
 * from a type attribute (e.g. the `%T` in `sret(%T)`).
 *
 * `LLVMIsAFunction` is the isa-style cast used to make the attribute
 * query safe: the LLVM-C attribute APIs only accept function-typed
 * `LLVMValueRef`s, and a callee may be a function pointer loaded from
 * a struct field rather than a function declaration.  Returns the
 * value cast as a function, or NULL when the cast fails. */
LLVMAttributeRef  LLVMGetEnumAttributeAtIndex(LLVMValueRef F, unsigned Idx,
                                              unsigned KindID);
LLVMTypeRef       LLVMGetTypeAttributeValue(LLVMAttributeRef A);
LLVMValueRef      LLVMIsAFunction(LLVMValueRef V);


/* ===================================================================
 * Pass Builder (new PM)
 * =================================================================== */

LLVMPassBuilderOptionsRef LLVMCreatePassBuilderOptions(void);
void  LLVMDisposePassBuilderOptions(LLVMPassBuilderOptionsRef Options);
void  LLVMPassBuilderOptionsSetVerifyEach(LLVMPassBuilderOptionsRef Options, LLVMBool VerifyEach);
int   LLVMRunPasses(LLVMModuleRef M, const char *Passes,
                    LLVMTargetMachineRef TM, LLVMPassBuilderOptionsRef Options);


/* ===================================================================
 * Utility
 * =================================================================== */

void LLVMDisposeMessage(char *Message);


/* ===================================================================
 * Debug Info (DWARF via DIBuilder)
 *
 * Enum-typed parameters (LLVMDWARFSourceLanguage, LLVMDWARFEmissionKind,
 * LLVMDWARFTypeEncoding, LLVMDIFlags, LLVMModuleFlagBehavior) are declared
 * here as plain `unsigned`/`int`. The real LLVM-C headers use enums, but
 * those are ABI-compatible with int, and declaring them as integers lets
 * the Cryo C-header extractor map them to u32/i32 (an unknown enum typedef
 * would otherwise map to an opaque pointer and break the call ABI).
 * All parameters are named so the extractor never sees an unnamed pointer.
 * =================================================================== */

LLVMDIBuilderRef LLVMCreateDIBuilder(LLVMModuleRef M);
void LLVMDIBuilderFinalize(LLVMDIBuilderRef Builder);

LLVMMetadataRef LLVMDIBuilderCreateFile(LLVMDIBuilderRef Builder,
    const char *Filename, size_t FilenameLen,
    const char *Directory, size_t DirectoryLen);

LLVMMetadataRef LLVMDIBuilderCreateCompileUnit(LLVMDIBuilderRef Builder,
    unsigned Lang, LLVMMetadataRef FileRef,
    const char *Producer, size_t ProducerLen,
    LLVMBool isOptimized, const char *Flags, size_t FlagsLen,
    unsigned RuntimeVer, const char *SplitName, size_t SplitNameLen,
    unsigned Kind, unsigned DWOId, LLVMBool SplitDebugInlining,
    LLVMBool DebugInfoForProfiling, const char *SysRoot, size_t SysRootLen,
    const char *SDK, size_t SDKLen);

LLVMMetadataRef LLVMDIBuilderCreateSubroutineType(LLVMDIBuilderRef Builder,
    LLVMMetadataRef File, LLVMMetadataRef *ParameterTypes,
    unsigned NumParameterTypes, int Flags);

LLVMMetadataRef LLVMDIBuilderCreateBasicType(LLVMDIBuilderRef Builder,
    const char *Name, size_t NameLen, uint64_t SizeInBits,
    unsigned Encoding, int Flags);

LLVMMetadataRef LLVMDIBuilderCreateFunction(LLVMDIBuilderRef Builder,
    LLVMMetadataRef Scope, const char *Name, size_t NameLen,
    const char *LinkageName, size_t LinkageNameLen, LLVMMetadataRef File,
    unsigned LineNo, LLVMMetadataRef Ty, LLVMBool IsLocalToUnit,
    LLVMBool IsDefinition, unsigned ScopeLine, int Flags, LLVMBool IsOptimized);

void LLVMSetSubprogram(LLVMValueRef Func, LLVMMetadataRef SP);

LLVMMetadataRef LLVMGetSubprogram(LLVMValueRef Func);

LLVMMetadataRef LLVMDIBuilderCreateDebugLocation(LLVMContextRef Ctx,
    unsigned Line, unsigned Column, LLVMMetadataRef Scope,
    LLVMMetadataRef InlinedAt);

void LLVMSetCurrentDebugLocation2(LLVMBuilderRef Builder, LLVMMetadataRef Loc);

LLVMMetadataRef LLVMValueAsMetadata(LLVMValueRef Val);

void LLVMAddModuleFlag(LLVMModuleRef M, unsigned Behavior,
    const char *Key, size_t KeyLen, LLVMMetadataRef Val);


#ifdef __cplusplus
}
#endif

#endif /* CRYO_LLVM_BINDINGS_H */
