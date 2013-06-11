//
// Copyright (c) 2008 Zvonimir Rakamaric (zvonimir@cs.utah.edu)
// This file is distributed under the MIT License. See LICENSE for details.
//
#include "SmackModuleGenerator.h"
#include "SmackInstGenerator.h"
#include "SmackRep.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CFG.h"
#include <sstream>
#include <stack>

namespace smack {

    using namespace std;
    using llvm::errs;

    llvm::RegisterPass<SmackModuleGenerator> X("smack", "SMACK generator pass");
    char SmackModuleGenerator::ID = 0;

    bool SmackModuleGenerator::runOnModule(llvm::Module &m) {

        program = new Program();
        SmackRep rep(&getAnalysis<llvm::DataLayout>());

        DEBUG(errs() << "Analyzing globals...\n");

        for (llvm::Module::const_global_iterator 
            x = m.global_begin(), e = m.global_end(); x != e; ++x) {
                
            string name = rep.id(x);
            program->addDecl(new ConstDecl(name, SmackRep::PTR_TYPE, true));  
            program->addDecl(new AxiomDecl(
                Expr::fn(SmackRep::STATIC, rep.obj(Expr::id(name))) ));
        }        
    
        // AXIOMS about variable uniqueness
        // NOTE: This should be covered by the "unique" annotation on the
        // declarations.  What is not covered is that their REFERENCES should
        // be unique...        
        // for (unsigned i=0; i<globals.size(); i++)
        //     for (unsigned j=i+1; j<globals.size(); j++)
        //         program->addDecl(new AxiomDecl(
        //             Expr::neq(Expr::id(globals[i]), Expr::id(globals[j])) ));

        DEBUG(errs() << "Analyzing functions...\n");
        
        set<pair<llvm::Function*,int> > missingDecls;

        for (llvm::Module::iterator func = m.begin(), e = m.end(); 
                func != e; ++func) {

            string name = rep.id(func);
            
            if (func->isDeclaration() || rep.isSmackName(name))
                continue;
            
            program->addDecl(new ConstDecl(name, SmackRep::PTR_TYPE, true));  
            program->addDecl(new AxiomDecl(
                Expr::fn(SmackRep::STATIC, rep.obj(Expr::id(name))) ));
        
            DEBUG(errs() << "Analyzing function: " << name << "\n");

            Procedure *proc = new Procedure(name);
            program->addProc(proc);
            
            // PARAMETERS
            for (llvm::Function::const_arg_iterator
                    arg = func->arg_begin(), e = func->arg_end(); arg != e; ++arg) {
                proc->addParam(rep.id(arg), rep.type(arg->getType()));
            }
        
            // RETURNS
            if (! func->getReturnType()->isVoidTy() )
                proc->addRet(SmackRep::RET_VAR, rep.type(func->getReturnType()));
        
            // MODIFIES
            proc->addMod(SmackRep::MEMORY);
            proc->addMod(SmackRep::ALLOC);

            // BODY
            if ( !func->isDeclaration() && !func->empty() 
                && !func->getEntryBlock().empty() ) {

                map<const llvm::BasicBlock*, Block*> known;
                stack<llvm::BasicBlock*> workStack;
                SmackInstGenerator igen(rep, *proc, known, missingDecls);

                llvm::BasicBlock& entry = func->getEntryBlock();
                workStack.push(&entry);
                known[&entry] = igen.createBlock();

                // INVARIANT: knownBlocks.CONTAINS(b) iff workStack.CONTAINS(b)
                // or workStack.CONTAINED(b) at some point in time.
                while (!workStack.empty()) {      
                    llvm::BasicBlock *b = workStack.top();
                    workStack.pop();
                
                    for (llvm::succ_iterator s = succ_begin(b),
                            e = succ_end(b); s != e; ++s) {
          
                        // uncovered basic block
                        if (known.count(*s) == 0) {
                            known[*s] = igen.createBlock();
                            workStack.push(*s);
                        }
                    }

                    // NOTE: here we are sure that all successor blocks have
                    // already been created, and are mapped for the visitor.

                    igen.setCurrBlock(known[b]);
                    igen.visit(b);
                }
            }

            DEBUG(errs() << "Finished analyzing function: " << name << "\n\n");
        }

        // Add any missing procedure declarations 
        // NOTE: for the moment, these correspond to VARARG procedures.
        for (set<pair<llvm::Function*,int> >::iterator d = missingDecls.begin();
            d != missingDecls.end(); ++d) {
 
            stringstream name;
            name << rep.id(d->first);
            if (d->first->isVarArg() && d->second > 0) {
                name << "#" << d->second;
            }
            Procedure *p = new Procedure(name.str());
            for (int i=0; i<d->second; i++) {
                stringstream param;
                param << "p" << i;
                p->addParam(param.str(), SmackRep::PTR_TYPE);
            }
            if (! d->first->getReturnType()->isVoidTy() )
                p->addRet(SmackRep::RET_VAR, SmackRep::PTR_TYPE);
            program->addProc(p);
        }
        
        return false;
    }

} // namespace smack