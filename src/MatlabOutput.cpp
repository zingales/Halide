#include "CodeGen_LLVM.h"
#include "CodeGen_Internal.h"
#include "LLVM_Headers.h"
#include "MatlabOutput.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace llvm;

namespace Halide {
namespace Internal {

class MatlabOutput : public OutputBase {
public:
    MatlabOutput(const std::string &filename)
        : filename(filename) {}

    // Define the mex wrapper API call for the given func.
    static void define_mex_wrapper(const LoweredFunc &func, Module *module) {
        LLVMContext &ctx = module->getContext();

        llvm::Type *i8_ty = llvm::Type::getInt8Ty(ctx);
        llvm::Type *i32_ty = llvm::Type::getInt32Ty(ctx);
        llvm::Type *void_ty = llvm::Type::getVoidTy(ctx);
        Value *zero = ConstantInt::get(i32_ty, 0);

        StructType *buffer_t_ty = module->getTypeByName("struct.buffer_t");
        internal_assert(buffer_t_ty) << "Did not find buffer_t in initial module\n";
        llvm::Type *mxArray_ptr_ty = i8_ty->getPointerTo();
        llvm::Type *mxArray_ptr_ptr_ty = mxArray_ptr_ty->getPointerTo();

        llvm::Function *halide_pipeline = module->getFunction(func.name);
        internal_assert(halide_pipeline) << "Did not find function '" << func.name << "' in module.\n";

        llvm::Function *matlab_init = module->getFunction("halide_matlab_init");
        internal_assert(matlab_init) << "Did not find function 'halide_matlab_init' in module.\n";
        llvm::Function *matlab_to_buffer_t = module->getFunction("halide_matlab_to_buffer_t");
        internal_assert(matlab_to_buffer_t) << "Did not find function 'halide_matlab_to_buffer_t' in module.\n";
        llvm::Function *mxGetScalar = module->getFunction("mxGetScalar");
        internal_assert(mxGetScalar) << "Did not find function 'mxGetScalar' in module.\n";
        llvm::Function *mexErrMsgIdAndTxt = module->getFunction("mexErrMsgIdAndTxt");
        internal_assert(mexErrMsgIdAndTxt) << "Did not find function 'mexErrMsgIdAndTxt' in module.\n";

        Constant *error_id_str = ConstantDataArray::getString(ctx, "Halide:" + func.name);
        GlobalValue *error_id = new GlobalVariable(*module,
                                                   error_id_str->getType(),
                                                   true,
                                                   llvm::GlobalValue::PrivateLinkage,
                                                   error_id_str,
                                                   func.name + "_error_id");

        Constant *error_arguments_str = ConstantDataArray::getString(ctx, "Expected %d arguments (got %d)\n");
        GlobalValue *error_arguments = new GlobalVariable(*module,
                                                          error_arguments_str->getType(),
                                                          true,
                                                          llvm::GlobalValue::PrivateLinkage,
                                                          error_arguments_str,
                                                          "error_arguments");

        Constant *error_halide_str = ConstantDataArray::getString(ctx, "Error in halide pipeline (%d)\n");
        GlobalValue *error_halide = new GlobalVariable(*module,
                                                       error_halide_str->getType(),
                                                       true,
                                                       llvm::GlobalValue::PrivateLinkage,
                                                       error_halide_str,
                                                       "error_halide_str");

        // Create the mexFunction function.
        llvm::Type *mex_arg_types[] = {
            i32_ty,
            mxArray_ptr_ptr_ty,
            i32_ty,
            mxArray_ptr_ptr_ty,
        };
        FunctionType *mex_ty = FunctionType::get(void_ty, mex_arg_types, false);
        llvm::Function *mex = llvm::Function::Create(mex_ty, llvm::GlobalValue::ExternalLinkage, "mexFunction", module);
        BasicBlock *entry = BasicBlock::Create(ctx, "entry", mex);

        IRBuilder<> ir(ctx);
        ir.SetInsertPoint(entry);

        // Call matlab init.
        ir.CreateCall(matlab_init);

        // Extract the argument values.
        llvm::Function::arg_iterator mex_args = mex->arg_begin();
        Value *nlhs = mex_args++;
        Value *plhs = mex_args++;
        Value *nrhs = mex_args++;
        Value *prhs = mex_args++;

        // Prepare all of the arguments.
        std::vector<Value *> args;

        Value *args_size = ConstantInt::get(i32_ty, (int)func.args.size());
        Value *n = ir.CreateAdd(nlhs, nrhs, "nlhs+nrhs");
        // Check if the number of arguments given is not correct.
        BasicBlock *setup_arg = BasicBlock::Create(ctx, "setup_arg", mex);
        BasicBlock *error = BasicBlock::Create(ctx, "error", mex);
        ir.CreateCondBr(ir.CreateICmpNE(args_size, n),
                        error,
                        setup_arg);


        // Create the error message for unexpected number of arguments.
        ir.SetInsertPoint(error);
        ir.CreateCall4(mexErrMsgIdAndTxt,
                       ir.CreateConstInBoundsGEP2_32(error_id, 0, 0),
                       ir.CreateConstInBoundsGEP2_32(error_arguments, 0, 0),
                       args_size,
                       n);
        ir.CreateRetVoid();

        error = BasicBlock::Create(ctx, "error", mex);
        ReturnInst::Create(ctx, error);

        ir.SetInsertPoint(setup_arg);

        for (size_t i = 0; i < func.args.size(); i++) {
            Constant *arg_idx = ConstantInt::get(i32_ty, i);

            // Check if this argument is to be found on the LHS or RHS
            // of the mexFunction.
            Value *is_rhs = ir.CreateICmpULT(arg_idx, nrhs, "is_rhs");

            // Get the index within the LHS or RHS.
            Value *n = ir.CreateSelect(is_rhs, arg_idx, ir.CreateSub(arg_idx, nrhs), "n");

            // Get the args for the LHS or RHS, depending on is_rhs.
            Value *pargs = ir.CreateSelect(is_rhs, prhs, plhs, "pargs");

            // Get a pointer to the arg.
            Value *mx_arg = ir.CreateLoad(ir.CreateGEP(pargs, n), "mx_arg");

            Value *arg;
            if (func.args[i].is_buffer) {
                // The arg is a buffer, we need to convert the arg
                // from mxArray to buffer_t.
                arg = ir.CreateAlloca(buffer_t_ty);

                // Call matlab_to_buffer_t with the argument.
                Value *result = ir.CreateCall3(matlab_to_buffer_t, arg_idx, mx_arg, arg);

                // Check for error.
                BasicBlock *next = BasicBlock::Create(ctx, "setup_arg", mex);
                ir.CreateCondBr(ir.CreateICmpNE(result, zero), error, next);
                ir.SetInsertPoint(next);
            } else {
                llvm::Type *arg_ty = llvm_type_of(&ctx, func.args[i].type);

                // Get a scalar for this arg.
                arg = ir.CreateCall(mxGetScalar, mx_arg);

                // mx_arg is a double, we need to convert it to the
                // type of the arg.
                switch (func.args[i].type.code) {
                case Type::Int: arg = ir.CreateFPToSI(arg, arg_ty); break;
                case Type::UInt: arg = ir.CreateFPToUI(arg, arg_ty); break;
                case Type::Float: arg = ir.CreateFPTrunc(arg, arg_ty); break;
                default: user_error << "Argument " << i << " is not a numeric scalar type.\n"; break;
                }
            }

            // Add the argument to the args list.
            args.push_back(arg);
        }

        // Call the halide pipeline.
        Value *result = ir.CreateCall(halide_pipeline, args);
        error = BasicBlock::Create(ctx, "error", mex);
        BasicBlock *success = BasicBlock::Create(ctx, "success", mex);
        ir.CreateCondBr(ir.CreateICmpNE(result, zero), error, success);

        ir.SetInsertPoint(error);
        ir.CreateCall3(mexErrMsgIdAndTxt,
                       ir.CreateConstInBoundsGEP2_32(error_id, 0, 0),
                       ir.CreateConstInBoundsGEP2_32(error_halide, 0, 0),
                       result);
        ir.CreateRetVoid();

        ir.SetInsertPoint(success);
        ir.CreateRetVoid();
    }

    void generate(const LoweredFunc &func) {
        CodeGen_LLVM *cg = CodeGen_LLVM::new_for_target(func.target.with_feature(Target::Matlab));
        cg->compile(func.body, func.name, func.args, func.images);

        Module *module = cg->get_module();

        // Add the mex callback wrapper to the module for the given func.
        define_mex_wrapper(func, module);

        // Get the target specific parser.
        string error_string;
        debug(1) << "Compiling to native code...\n";
        debug(2) << "Target triple: " << module->getTargetTriple() << "\n";

        const llvm::Target *target = TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
        if (!target) {
            cout << error_string << endl;
            TargetRegistry::printRegisteredTargetsForVersion();
        }
        internal_assert(target) << "Could not create target\n";

        debug(2) << "Selected target: " << target->getName() << "\n";

        TargetOptions options;
        options.LessPreciseFPMADOption = true;
        options.NoFramePointerElim = false;
        options.AllowFPOpFusion = FPOpFusion::Fast;
        options.UnsafeFPMath = true;
        options.NoInfsFPMath = true;
        options.NoNaNsFPMath = true;
        options.HonorSignDependentRoundingFPMathOption = false;
        options.UseSoftFloat = false;
        options.FloatABIType =
            cg->use_soft_float_abi() ? FloatABI::Soft : FloatABI::Hard;
        options.NoZerosInBSS = false;
        options.GuaranteedTailCallOpt = false;
        options.DisableTailCalls = false;
        options.StackAlignmentOverride = 0;
        options.TrapFuncName = "";
        options.PositionIndependentExecutable = true;
        options.UseInitArray = false;

        TargetMachine *target_machine =
            target->createTargetMachine(module->getTargetTriple(),
                                        cg->mcpu(), cg->mattrs(),
                                        options,
                                        Reloc::PIC_,
                                        CodeModel::Default,
                                        CodeGenOpt::Aggressive);

        internal_assert(target_machine) << "Could not allocate target machine!\n";

        // Build up all of the passes that we want to do to the module.
        PassManager pass_manager;

        // Add an appropriate TargetLibraryInfo pass for the module's triple.
        pass_manager.add(new TargetLibraryInfo(Triple(module->getTargetTriple())));

#if LLVM_VERSION < 33
        pass_manager.add(new TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                                 target_machine->getVectorTargetTransformInfo()));
#else
        target_machine->addAnalysisPasses(pass_manager);
#endif

#if LLVM_VERSION < 35
        DataLayout *layout = new DataLayout(module);
        debug(2) << "Data layout: " << layout->getStringRepresentation();
        pass_manager.add(layout);
#endif

        // Make sure things marked as always-inline get inlined
        pass_manager.add(createAlwaysInlinerPass());

        // Override default to generate verbose assembly.
        target_machine->setAsmVerbosityDefault(true);

        raw_fd_ostream *raw_out = new_raw_fd_ostream(filename);
        formatted_raw_ostream *out = new formatted_raw_ostream(*raw_out);

        // Ask the target to add backend passes as necessary.
        target_machine->addPassesToEmitFile(pass_manager, *out, TargetMachine::CGFT_ObjectFile);

        pass_manager.run(*module);

        delete out;
        delete raw_out;

        delete target_machine;
        delete cg;
    }

private:
    std::string filename;
};

}  // namespace Internal

Output output_matlab(const std::string &filename) {
    return Output(new Internal::MatlabOutput(filename));
}

}  // namespace Halide
