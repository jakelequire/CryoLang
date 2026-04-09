/*
 * llvm_bindings.h — Thin LLVM-C API wrapper for the Cryo compiler.
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

#ifdef __cplusplus
extern "C" {
#endif


/* ===================================================================
 * Opaque handle types — all void* on the Cryo side
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
void           LLVMDumpModule(LLVMModuleRef M);
LLVMContextRef LLVMGetModuleContext(LLVMModuleRef M);


/* ===================================================================
 * Types — Integer
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
 * Types — Floating Point
 * =================================================================== */

LLVMTypeRef LLVMFloatTypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMDoubleTypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMFloatType(void);
LLVMTypeRef LLVMDoubleType(void);


/* ===================================================================
 * Types — Void / Label / Pointer
 * =================================================================== */

LLVMTypeRef LLVMVoidTypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMVoidType(void);
LLVMTypeRef LLVMPointerType(LLVMTypeRef ElementType, unsigned AddressSpace);
LLVMTypeRef LLVMPointerTypeInContext(LLVMContextRef C, unsigned AddressSpace);


/* ===================================================================
 * Types — Function
 * =================================================================== */

LLVMTypeRef LLVMFunctionType(LLVMTypeRef ReturnType, LLVMTypeRef *ParamTypes,
                             unsigned ParamCount, LLVMBool IsVarArg);
unsigned    LLVMCountParamTypes(LLVMTypeRef FunctionTy);
LLVMBool    LLVMIsFunctionVarArg(LLVMTypeRef FunctionTy);
LLVMTypeRef LLVMGetReturnType(LLVMTypeRef FunctionTy);


/* ===================================================================
 * Types — Struct
 * =================================================================== */

LLVMTypeRef LLVMStructTypeInContext(LLVMContextRef C, LLVMTypeRef *ElementTypes,
                                    unsigned ElementCount, LLVMBool Packed);
LLVMTypeRef LLVMStructCreateNamed(LLVMContextRef C, const char *Name);
void        LLVMStructSetBody(LLVMTypeRef StructTy, LLVMTypeRef *ElementTypes,
                              unsigned ElementCount, LLVMBool Packed);
unsigned    LLVMCountStructElementTypes(LLVMTypeRef StructTy);
LLVMTypeRef LLVMStructGetTypeAtIndex(LLVMTypeRef StructTy, unsigned i);
LLVMBool    LLVMIsOpaqueStruct(LLVMTypeRef StructTy);


/* ===================================================================
 * Types — Array
 * =================================================================== */

LLVMTypeRef LLVMArrayType(LLVMTypeRef ElementType, unsigned ElementCount);
LLVMTypeRef LLVMArrayType2(LLVMTypeRef ElementType, unsigned long long ElementCount);
unsigned    LLVMGetArrayLength(LLVMTypeRef ArrayTy);
LLVMTypeRef LLVMGetElementType(LLVMTypeRef Ty);


/* ===================================================================
 * Values — General
 * =================================================================== */

LLVMTypeRef    LLVMTypeOf(LLVMValueRef Val);
const char    *LLVMGetValueName2(LLVMValueRef Val, unsigned long *Length);
void           LLVMSetValueName2(LLVMValueRef Val, const char *Name, unsigned long NameLen);
void           LLVMDumpValue(LLVMValueRef Val);
char          *LLVMPrintValueToString(LLVMValueRef Val);
void           LLVMSetLinkage(LLVMValueRef Global, int Linkage);
int            LLVMGetLinkage(LLVMValueRef Global);


/* ===================================================================
 * Constants
 * =================================================================== */

LLVMValueRef LLVMConstInt(LLVMTypeRef IntTy, unsigned long long N, LLVMBool SignExtend);
LLVMValueRef LLVMConstReal(LLVMTypeRef RealTy, double N);
LLVMValueRef LLVMConstNull(LLVMTypeRef Ty);
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
void         LLVMSetInitializer(LLVMValueRef GlobalVar, LLVMValueRef ConstantVal);
void         LLVMSetGlobalConstant(LLVMValueRef GlobalVar, LLVMBool IsConstant);
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
LLVMBasicBlockRef LLVMGetLastBasicBlock(LLVMValueRef Fn);


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


/* ===================================================================
 * Builder
 * =================================================================== */

LLVMBuilderRef LLVMCreateBuilder(void);
LLVMBuilderRef LLVMCreateBuilderInContext(LLVMContextRef C);
void           LLVMDisposeBuilder(LLVMBuilderRef Builder);
void           LLVMPositionBuilderAtEnd(LLVMBuilderRef Builder, LLVMBasicBlockRef Block);
LLVMBasicBlockRef LLVMGetInsertBlock(LLVMBuilderRef Builder);


/* ===================================================================
 * Builder — Terminators
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
 * Builder — Arithmetic
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
 * Builder — Bitwise
 * =================================================================== */

LLVMValueRef LLVMBuildShl(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildLShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildAShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildAnd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildOr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildXor(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS, const char *Name);
LLVMValueRef LLVMBuildNot(LLVMBuilderRef B, LLVMValueRef V, const char *Name);


/* ===================================================================
 * Builder — Comparisons
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
 * Builder — Memory
 * =================================================================== */

LLVMValueRef LLVMBuildAlloca(LLVMBuilderRef B, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMBuildLoad2(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef PointerVal,
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
 * Builder — Casts
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
 * Builder — Other
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


#ifdef __cplusplus
}
#endif

#endif /* CRYO_LLVM_BINDINGS_H */
