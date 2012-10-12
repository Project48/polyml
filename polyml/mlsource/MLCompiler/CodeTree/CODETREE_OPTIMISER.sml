(*
    Copyright (c) 2012 David C.J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*)

functor CODETREE_OPTIMISER(
    structure BASECODETREE: BaseCodeTreeSig
    structure CODETREE_FUNCTIONS: CodetreeFunctionsSig

    sharing BASECODETREE.Sharing = CODETREE_FUNCTIONS.Sharing

) :
    sig
        type codetree and optVal
        val codetreeOptimiser: codetree  * (codetree * int -> codetree) * int -> optVal * int
        structure Sharing: sig type codetree = codetree and optVal = optVal end
    end
=
struct
    open BASECODETREE
    open Address
    open CODETREE_FUNCTIONS
    open StretchArray
    
    infix 9 sub
    
    exception InternalError = Misc.InternalError

    val isConstnt    = fn (Constnt _)    => true | _ => false
    val isCodeNil    = fn CodeNil        => true | _ => false

    type loadForm = 
    { (* Load a value. *)
        addr : int, 
        level: int, 
        fpRel: bool,
        lastRef: bool
    }

    (* gets a value from the run-time system *)
    val ioOp : int -> machineWord = RunCall.run_call1 RuntimeCalls.POLY_SYS_io_operation;

    val word0 = toMachineWord 0;
    val word1 = toMachineWord 1;
  
    val False = word0;   
    val True  = word1;   

    (* since code generator relies on these representations,
       we may as well export them *)
    val rtsFunction = mkConst o ioOp

  (* Remove redundant declarations from the code.  This reduces
     the size of the data structure we retain for inline functions
     and speeds up compilation.  More importantly it removes internal
     functions which have been completely inlined.  These can mess up
     the optimisation which detects which parameters to a recursive
     function are unchanged.   It actually duplicates work that is
     done later in preCode but adding this function significantly
     reduced compilation time by reducing the amount of garbage
     created through inline expansion. DCJM 29/12/00. *)
  (* This also ensures that recursive references are converted into
     the correct CLOS(0,0) form. DCJM 23/1/01. *)
   fun cleanProc (pt: codetree, prev: loadForm * int * int -> codetree,
                  nestingDepth, locals): codetree =
   let
        fun cleanLambda(myAddr,
            {body, isInline, name, argTypes, resultType, level=nestingDepth, localCount, ...}) =
        let
            (* Start a new level. *)
            fun lookup(ext as {addr, fpRel, ...}, level, depth) =
                if level = 0
                then if addr = myAddr andalso fpRel
                then (* It's a recursive reference. *)
                        mkRecLoad(depth-nestingDepth)
                else 
                (
                    if addr >= 0 andalso fpRel
                    then Array.update(locals, addr, true)
                    else (); (* Recursive *)
                    Extract ext
                )
                else prev(ext, level-1, depth);

            val newLocals = Array.array (localCount (* Initial size. *), false);
            val bodyCode = cleanProc(body, lookup, nestingDepth, newLocals)
        in
            Lambda{body=bodyCode, isInline=isInline, name=name,
                   closure=[], argTypes=argTypes, resultType=resultType, level=nestingDepth,
                   closureRefs=0, makeClosure=false, localCount=localCount, argLifetimes = []}
        end

        and cleanCode (Newenv decs) =
            let
                fun cleanDec(myAddr, Lambda lam) = cleanLambda(myAddr, lam)
                |   cleanDec(_, d) = cleanCode d;

                (* Process the declarations in reverse order. *)
                fun processDecs [] = []
                 |  processDecs(Declar{value, addr, references} :: rest) =
                    let
                        (* Clear the entry.  I think it's possible that
                           addresses have been reused in other blocks
                           so do this just in case. *)
                        val _ = Array.update(locals, addr, false)
                        val processedRest = processDecs rest
                    in
                        (* If this is used or if it has side-effects we
                           must include it otherwise we can ignore it. *)
                        if Array.sub(locals, addr) orelse not (sideEffectFree value)
                        then Declar{value=cleanDec(addr, value), addr=addr,
                                    references=references} :: processedRest
                        else processedRest
                    end

                 |  processDecs(MutualDecs decs :: rest) =
                    let
                        (* Clear the entries just in case the addresses are reused. *)
                        fun setEntry(Declar{addr, ...}) = Array.update(locals, addr, false)
                          | setEntry _ = raise InternalError "setEntry: unknown instr"
                        val _ = List.app setEntry decs
                        val processedRest = processDecs rest

                        (* We now know the entries that have actually been used
                           in the rest of the code.  We need to include those
                           declarations and any that they use. *)
                        fun processMutuals [] excluded true =
                                (* If we have included a function in this
                                   pass we have to reprocess the list of
                                   those we excluded before. *)
                                processMutuals excluded [] false
                         |  processMutuals [] _ false =
                                (* We didn't add anything more - finish *) []
                         |  processMutuals(
                                (this as Declar{addr, value, references}) :: rest)
                                        excluded added =
                            if not (Array.sub(locals, addr))
                            then (* Put this on the excluded list. *)
                                processMutuals rest (this::excluded) added
                            else
                            let
                                (* Process this entry - it may cause other
                                   entries to become "used". *)
                                val newEntry =
                                    Declar{value=cleanDec(addr, value), addr=addr,
                                        references=references}
                            in
                                newEntry :: processMutuals rest excluded true
                            end
                          | processMutuals _ _ _ = 
                                raise InternalError "processMutual: unknown instr"
                        val processedDecs = processMutuals decs nil false
                    in
                        case processedDecs of
                            [] => processedRest (* None at all. *)
                        |   [oneDec] => oneDec :: processedRest
                        |   mutuals => MutualDecs mutuals :: processedRest
                    end
                 
                 |  processDecs(Newenv decs :: rest) = (* Expand out blocks. *)
                    let
                        val processedRest = processDecs rest
                        val processedDecs = processDecs decs
                    in
                        processedDecs @ processedRest
                    end

                 |  processDecs(exp :: rest) =
                    let
                        (* Either the result expression or part of an expression
                           being evaluated for its side-effects.  We can
                           eliminate it if it doesn't actually have a side-effect
                           except if it is the result.
                           Note: we have to process the rest of the list first
                           because the code for SetContainer checks whether the
                           container is used. *)
                        val processedRest = processDecs rest
                        val newExp = cleanCode exp
                    in
                        if sideEffectFree newExp andalso not(null processedRest)
                        then processedRest
                        else newExp :: processedRest
                    end

                val res = processDecs decs
            in
                (* We need a Newenv entry except for singleton expressions. *)
                wrapEnv res
            end (* Newenv *)

         |  cleanCode (dec as Extract(ext as {addr, level, fpRel, ...})) =
                (* If this is a local we need to mark it as used. *)
                if level = 0
                then
                    (
                    (* On this level. *)
                    if addr >= 0 andalso fpRel
                    then (* Local *) Array.update(locals, addr, true)
                    else (); (* Argument or recursive - ignore it. *)
                    dec
                    )
                else (* Non-local.  This may be a recursive call. *)
                    prev(ext, level-1, nestingDepth)

         |  cleanCode (Lambda lam) = cleanLambda(0, lam)

            (* All the other case simply map cleanCode over the tree. *)
         |  cleanCode MatchFail = MatchFail

         |  cleanCode (AltMatch(a, b)) = AltMatch(cleanCode a, cleanCode b)

         |  cleanCode (c as Constnt _) = c

         |  cleanCode (Indirect{base, offset}) =
                Indirect{base=cleanCode base, offset=offset}

         |  cleanCode (Eval{function, argList, earlyEval, resultType}) =
                Eval{function=cleanCode function, argList = map (fn (c, t) => (cleanCode c, t)) argList,
                     earlyEval=earlyEval, resultType=resultType}

         |  cleanCode(Cond(test, thenpt, elsept)) =
                Cond(cleanCode test, cleanCode thenpt, cleanCode elsept)

         |  cleanCode(BeginLoop{loop=body, arguments=argList, ...}) =
            let
                val processedBody = cleanCode body
                fun copyDec(Declar{addr, value, ...}, typ) =
                        (mkDec(addr, cleanCode value), typ)
                  | copyDec _ = raise InternalError "copyDec: not a declaration"
                val newargs = map copyDec argList
            in
                BeginLoop{loop=processedBody, arguments=newargs}
            end

         |  cleanCode(Loop args) = Loop(map (fn (c, t) => (cleanCode c, t)) args)

         |  cleanCode(Raise r) = Raise(cleanCode r)

         |  cleanCode(Ldexc) = Ldexc

         |  cleanCode(Handle{exp, handler}) =
                Handle{exp = cleanCode exp, handler = cleanCode handler}

         |  cleanCode(Recconstr decs) = Recconstr(map cleanCode decs)

         |  cleanCode(c as Container _) = c

         |  cleanCode(SetContainer {container, tuple, size}) =
               let
                (* If the container is unused we don't need to set it.
                   The container won't be created either. *)
                  val used =
                      case container of
                        Extract{addr, level=0, fpRel=true, ...} =>
                            addr <= 0 orelse Array.sub(locals, addr)
                      | _ => true (* Assume it is. *)
               in
                (* Disable this for the moment - it's probably not very useful
                   anyway.  It doesn't work at the moment because we sometimes
                   make a local declaration point at another variable (in
                   pushContainer and replaceContainerDec).  The
                   new variable may be set but not used while the old variable
                   is used but not set.  *)
                if not used andalso false
                then CodeZero (* Return something. *)
                else
                (* Push the container down the tree and then process it. If we've
                   expanded an inline function we want to be able to find any
                   tuple we're creating. *)
                case tuple of
                    Cond _ => cleanCode(mkSetContainer(container, tuple, size))
                |   Newenv _ => cleanCode(mkSetContainer(container, tuple, size))
                |   r as Raise _ =>
                        (* If we're raising an exception we don't need to set the container. *)
                        cleanCode r
                |   _ => SetContainer{container = cleanCode container,
                            tuple = cleanCode tuple, size = size}
               end

         |  cleanCode(TupleFromContainer(container, size)) =
               TupleFromContainer(cleanCode container, size)

         |  cleanCode CodeNil = CodeNil

         |  cleanCode (TagTest{test, tag, maxTag}) =
                TagTest{test=cleanCode test, tag=tag, maxTag=maxTag}

         |  cleanCode(IndirectVariable{base, offset}) =
                IndirectVariable{base=cleanCode base, offset=cleanCode offset}

         |  cleanCode(TupleVariable(vars, length)) =
            let
                fun cleanTuple(VarTupleSingle{source, destOffset}) =
                        VarTupleSingle{source=cleanCode source, destOffset=cleanCode destOffset}
                |   cleanTuple(VarTupleMultiple{base, length, destOffset, sourceOffset}) =
                        VarTupleMultiple{base=cleanCode base, length=cleanCode length,
                                         destOffset=cleanCode destOffset, sourceOffset=cleanCode sourceOffset}
            in
                TupleVariable(map cleanTuple vars, cleanCode length)
            end

         |  cleanCode (Declar _) = raise InternalError "cleanCode: Declar"

         |  cleanCode (MutualDecs _) = raise InternalError "cleanCode: MutualDecs"

         |  cleanCode (Case _) = raise InternalError "cleanCode: Case"

         |  cleanCode (Global _) = raise InternalError "cleanCode: Global"
         
         |  cleanCode (KillItems _) = raise InternalError "cleanCode: StaticLinkCall"
   in
        cleanCode pt
   end (* cleanProc *);



(************************************************************************)

  val initTrans = 10; (* Initial size of arrays. *)

(*****************************************************************************)
(*                  changeLevel                                              *)
(*****************************************************************************)
  (* Change the level of an entry if necessary. This *)
  (* happens if we have a function inside an inline function. *)
  fun changeLevel entry 0 = entry (* No change needed*)
   | changeLevel entry correction =
      let
      fun changeL(ext as Extract{addr, level, fpRel, ...}, nesting) =
            if level >= 0 andalso level < nesting
                (* We enter declarations with level = ~1 for recursive
                   calls when processing mutual recursion. *)
            then ext (* It's local to the function(s) we're processing. *)
            else mkGenLoad (addr, level + correction, fpRel, false)

        | changeL(Lambda{body, isInline, name, closure, argTypes, resultType,
                         level, closureRefs, makeClosure, localCount, ...}, nesting) =
            Lambda{body = changeL(body, nesting+1), isInline = isInline,
                   name = name, closure = closure, argTypes = argTypes, resultType=resultType,
                   level = level + correction, closureRefs = closureRefs,
                   makeClosure = makeClosure, localCount=localCount, argLifetimes = []}

        | changeL(Eval{function, argList, earlyEval, resultType}, nesting) =
            Eval{function = changeL(function, nesting),
                 argList = map (fn (a, t) => (changeL(a, nesting), t)) argList,
                 earlyEval = earlyEval, resultType=resultType}

        | changeL(Indirect{ base, offset }, nesting) =
            Indirect{base = changeL(base, nesting), offset = offset }

        | changeL(Declar{value, addr, ...}, nesting) =
            mkDec(addr, changeL(value, nesting))

        | changeL(Newenv l, nesting) =
            Newenv(map(fn d => changeL(d, nesting)) l)

        | changeL(c as Container _, _) = c

        | changeL(TupleFromContainer(container, size), nesting) =
            TupleFromContainer(changeL(container, nesting), size)

        | changeL(_, _) =
            (* The code we produce in these inline functions is very limited. *)
               raise InternalError "changeL: Unknown code"

    in
        case optGeneral entry of
            gen as Extract _ =>
                optVal 
                {
                    general = changeL(gen, 0),
                    special = optSpecial entry,
                    environ = optEnviron entry,
                    decs    = [],
                    recCall = optRec entry
                }

        |   Constnt _ => entry

        |   gen as Lambda _ =>
                optVal
                {
                    general = changeL(gen, 0),
                    special = optSpecial entry,
                    environ = optEnviron entry,
                    decs    = [],
                    recCall = optRec entry
                }
            
        | _ => raise InternalError "changeLevel: Unknown entry"
    end
  (* end changeLevel *)

(*****************************************************************************)
(*                  optimiseProc                                             *)
(*****************************************************************************)
  fun optimiseProc
    {pt : codetree,
     lookupNewAddr : loadForm * int * int -> optVal,
     lookupOldAddr : loadForm * int * int -> optVal,
     enterDec : int * optVal -> unit,
     enterNewDec : int * optVal -> unit,
     nestingOfThisProcedure : int,
     spval : int ref,
     earlyInline : bool,
     evaluate : codetree * int -> codetree,
     tailCallEntry: bool ref option,
     recursiveExpansions:
        (((codetree * argumentType) list * bool * int -> (codetree * argumentType) list) * bool ref) list,
     maxInlineSize: int} =
  (* earlyInline is true if we are expanding a procedure declared 
     "early inline". *)
  (* spval is the Declaration counter. Normally ref(1) except when expanding
     an inline procedure. *)
  (* tailCallEntry is NONE if this is not an inline function and SOME r if
     it is.  r is set to true if a tail recursive LOOP instruction is generated. *)
  let
(*****************************************************************************)
(*                  newDecl (inside optimiseProc)                            *)
(*****************************************************************************)
    (* Puts a new declaration into a table. Used for both local declarations
       and also parameters to inline procedures. "setTab" is the table to
       put the entry in and "pt" is the value to be put in the table. We try
       to optimise various cases, such as constants, where a value is declared 
       but where it is more efficient when it is used to return the value
       itself rather than an instruction to load the value. *)
    fun stripDecs (ov : optVal) : optVal =
      case optDecs ov of 
        [] => ov
      | _  =>
        optVal 
          {
             general = optGeneral ov,
             special = optSpecial ov,
             environ = optEnviron ov,
             decs    = [],
             recCall = optRec ov
          };
       
    fun newDecl (setTab, ins, addrs, pushProc) : codetree list =
    let
        val gen  = optGeneral ins;
    in
        case gen of
            Constnt _ => 
            let (* No need to generate code. *)
                val spec = optSpecial ins
                val ov = 
                    (* If it is a non-inline procedure it must be declared. *)
                    case (spec, pushProc) of
                        (Lambda{isInline=NonInline, ...}, true) => simpleOptVal gen
                    |   _ => stripDecs ins  (* Remove the declarations before entering it. *)
                val () = setTab (addrs, ov)
            in
                (* Just return the declarations. *)
                optDecs ins 
            end

        |   Extract { level = 0, ...} =>
            let
                (* Declaration is simply giving a new name to a local
                   - can ignore this declaration. *) 
                val optVal = stripDecs ins (* Simply copy the entry.  *)
                val () = setTab (addrs, optVal)
            in
                optDecs ins
            end

            (* This next case was commented out by AHL.  It could probably be reinstated. *)
(*        |   Indirect {base, offset} =>
            let
                (* It is safe to defer an indirection if we can. For instance,
                   in ML fun f (a as (b,c)) will generate declarations of b and c
                   as indirections on a. If b and c are not used immediately there
                   is no point in loading them  (it only uses up the registers).
                   Once they are actually used they will be loaded into registers
                   and those registers will be cached by the normal register caching
                   scheme, so that if used again soon after they will not be
                   reloaded. *)
                fun optSetTab (i, v) =
                    setTab
                    (i,
                    optVal 
                    { (* Add on the indirection. *)
                        general = mkInd (offset, optGeneral v),
                        special = optSpecial v,
                        environ = optEnviron v,
                        decs    = optDecs v,
                        recCall = optRec v
                    })
            in
                (* Take off the indirection from the value to be declared and add
                   it to the load instruction. This causes the indirection to be
                   deferred until the value is actually used. *)
                newDecl(optSetTab,
                    optVal 
                    {
                        general = base,
                        special = optSpecial ins,
                        environ = optEnviron ins,
                        decs    = optDecs ins,
                        recCall = optRec ins
                    }, addrs, pushProc)
            end*)

        |   _ =>
            let (* Declare an identifier to have this value. *)
                val decSpval = ! spval; 
                val ()       = spval := decSpval + 1 ;
            
                (* The table entry is similar to the result of the expression except
                    that the declarations are taken off and put into the containing
                    block, and the general value is put into a local variable and
                    replaced by an instruction to load from there. If the special
                    is a non-inline procedure it is removed. Non-inline procedures
                    are returned by copyLambda so that they can be inserted inline
                    if they are immediately called (e.g. a catch phrase) but if they
                    are declared they are created as normal procedures. We don't do
                    this for parameters to inline procedures so that lambda-expressions
                    passed to inline procedures will be expanded inline if they are
                    only called inside the inline procedure.
                    e.g. for(..., proc(..)(...)) will be expanded inline. *)
                val spec = optSpecial ins;
                val optSpec =
                    case (spec, pushProc) of
                        (Lambda{isInline=NonInline, ...}, true) => CodeNil
                    |   _ => spec
              
                val optV = 
                    optVal 
                    {
                        general = mkLoad (decSpval, 0),
                        special = optSpec,
                        environ = optEnviron ins,
                        decs    = [],
                        recCall = optRec ins
                    }
            
                val () = setTab (addrs, optV)
            in
                optDecs ins @ [mkDecRef(gen, decSpval, 0)]
            end
        end (* newDecl *)

    (* Handle an Indirect node or the equivalent in a variable tuple. *)
    fun doIndirection(source, offset) =
        case (optSpecial source, optGeneral source) of
          (spec as Recconstr _, _) =>
            let
                (* The "special" entry we've found is a record.  That means that
                   we are taking a field from a record we made earlier and so we
                   should be able to get the original code we used when we made
                   the record.  That might mean the record is never used and
                   we can optimise away the construction of it completely. The
                   entry we get back from findEntryInBlock will either be a
                   constant or a load.  In that case we need to look it up in
                   the environment we used for the record to give us an optVal.
                   The previous code in newDecl, commented out by AHL, also put
                   indirections into the table.  To save having the various cases
                   in here we simply call optimiseProc which will deal with them.
                   DCJM 9/1/01. *)
                val specEntry = findEntryInBlock spec offset;
                val newCode =
                 optimiseProc 
                   {pt=specEntry,
                    lookupNewAddr=errorEnv, (* We must always look up old addresses. *)
                    lookupOldAddr=optEnviron source,
                    enterDec=enterDec, (* should not be used *)
                    enterNewDec=enterNewDec, (* should not be used *)
                    nestingOfThisProcedure=nestingOfThisProcedure,
                    spval=spval,
                    earlyInline=earlyInline,
                    evaluate=evaluate,
                    tailCallEntry=NONE,
                    recursiveExpansions=recursiveExpansions,
                    maxInlineSize=maxInlineSize}
            in
              optVal 
                {
                  general = optGeneral newCode,
                  special = optSpecial newCode,
                  environ = optEnviron newCode,
                  decs    = optDecs source @ optDecs newCode,
                  recCall = optRec newCode
                }
            end

        | (_ , gen as Constnt _ ) => (* General is a constant -  Do the selection now. *)
            optVal 
              {
                general = findEntryInBlock gen offset,
                special = CodeNil,
                environ = errorEnv,
                decs    = optDecs source,
                recCall = ref false
              }
                           
        | (_, _) => (* No special case possible. *)
              optVal 
            {
              general = mkInd (offset, optGeneral source),
              special = CodeNil,
              environ = errorEnv,
              decs    = optDecs source,
              recCall = ref false
            }

(*****************************************************************************)
(*                  optimise (inside optimiseProc)                           *)
(*****************************************************************************)
    fun getGeneral ov =
        (
        case optDecs ov of
          []   => optGeneral ov
        | decs => mkEnv (decs @ [optGeneral ov])
        )

    (* The main optimisation routine. *)
    (* Returns only the general value from an expression. *)
    fun generalOptimise(pt, tailCall) = getGeneral(optimise(pt, tailCall))

    and general pt = generalOptimise(pt, NONE)

    and optimise (pt as MatchFail, _) = simpleOptVal pt

     |  optimise (AltMatch(a, b), _) =
        simpleOptVal(AltMatch(general a, general b))
    
     |  optimise (CodeNil, _) = simpleOptVal CodeNil
        
     |  optimise (Eval{function, argList, earlyEval, resultType}, tailCall) =
        let
          (* Get the function to be called and see if it is inline or
             a lambda expression. *)
          val funct : optVal = optimise(function, NONE);
          val foptRec = optRec funct
               
          (* There are essentially two cases to consider - the procedure
             may be "inline" in which case it must be expanded as a block,
             or it must be called. *)
          fun notInlineCall(recCall:( ((codetree * argumentType) list * bool * int -> (codetree * argumentType) list) * bool ref)option) = 
            let
            val argsAreConstants = ref true;

            fun copyArg(arg, argType) =
              let
                 val copied = general arg;
              in
                (* Check for early evaluation. *)
                if not (isConstnt copied) then argsAreConstants := false else ();
                (copied, argType)
             end
   
            val copiedArgs = map copyArg argList;
            val gen = optGeneral funct
            and early = earlyEval orelse earlyInline

            (* If the procedure was declared as early or is inside an inline
               procedure declared as early we can try to evaluate it now.
               Also if it is a call to an RTS function (which may actually
               be code-generated inline by G_CODE) we can evaluate it if
               it's safe. *)
            val evalEarly =
                ! argsAreConstants andalso isConstnt (optGeneral funct) andalso
                (early orelse
                 (case optGeneral funct of
                    Constnt w =>
                        isIoAddress(toAddress w) andalso earlyRtsCall(w, copiedArgs)
                  | _ => false
                 )
                )
            
            val evCopiedCode = 
              if evalEarly
              then evaluate (Eval {function = gen, argList = copiedArgs, earlyEval = early, resultType=resultType}, !spval+1)
              else case recCall of
                (* This is a recursive call to a function we're expanding.
                   Is it tail recursive?  We may have several levels of
                   expansion. *)
                SOME (filterArgs, optr) =>
                    if (case tailCall of
                            SOME tCall => optr = tCall (* same reference? *)
                        | NONE => false)
                    then Loop (filterArgs(copiedArgs, true, nestingOfThisProcedure))
                    else Eval {function = gen,
                            argList = filterArgs(copiedArgs, false, nestingOfThisProcedure),
                            earlyEval = early, resultType=resultType}
                 (* Not a recursive expansion. *)
               | NONE => Eval {function = gen, argList = copiedArgs, earlyEval = early, resultType=resultType}
         in
            optVal 
              {
                general = evCopiedCode,
                special = CodeNil,
                environ = errorEnv,
                decs    = optDecs funct,
                recCall = ref false
              }
          end (* notInlineCall *)

        in
          case (List.find (fn (_, r) => r = foptRec) recursiveExpansions, optSpecial funct) of
             (recCall as SOME _, _) =>
                 (* We're already expanding this function - don't recursively expand
                    it.  Either loop or generate a function call. *)
                notInlineCall recCall
         | (_,
            Lambda { isInline, body=lambdaBody, name=lambdaName, argTypes, ...}) =>
            let
           (* Calling inline proc or a lambda expression which is just called.
              The procedure is replaced with a block containing declarations
              of the parameters.  We need a new table here because the addresses
              we use to index it are the addresses which are local to the function.
              New addresses are created in the range of the surrounding function. *)
            val localVec = stretchArray (initTrans, NONE);
            val paramVec = stretchArray (initTrans, NONE);
            val localNewVec = stretchArray (initTrans, NONE);
            
                (* copies the argument list. *)
            fun copy []     _          = [] : codetree list
              | copy ((h, _)::t) argAddress =
              let
			    fun setTab (index, v) = update (paramVec, ~index, SOME v);
                (* Make the declaration, picking out constants, inline
                   procedures and load-and-stores. These are entered in the
                   table, but nil is returned by "newDecl". *)
                val lapt = newDecl (setTab, optimise(h, NONE), argAddress, false);
              in (* Now process the rest of the declarations. *)
                lapt @ copy t (argAddress + 1)
              end (* end copy *)

            val nArgs = List.length argList
            val copiedArgs = copy argList (~nArgs);
            (* Create an immutable vector from the parameter array to reduce the
               amount of mutable data, *)
            val frozenParams = StretchArray.vector paramVec

            (* All declarations should be of positive addresses. *)
            fun setNewTabForInline (addr, v) = update (localNewVec, addr, SOME v)

            fun setTabForInline (index, v) =
                (
                    case optGeneral v of
                        Extract{addr, ...} =>
                            if addr <= 0 then ()
                            else setNewTabForInline(addr, v)
                    |   _ => ();
                    update (localVec, index, SOME v)
                )

            fun lookupLocalNewAddr (ext as { addr, ...}, depth, levels) =
                    (* It may be local to this function or to the surrounding scope. *)
                if levels <> 0 orelse addr <= 0
                then lookupNewAddr(ext, depth, levels)
                else case localNewVec sub addr of
                        SOME v => changeLevel v (depth - nestingOfThisProcedure)
                    |   NONE => lookupNewAddr(ext, depth, levels);

            val copiedBody =
                if isInline = MaybeInline orelse isInline = OnlyInline
                then(* It's a function the front-end has explicitly inlined.
                       It can't be directly recursive.  If it turns out to
                       be indirectly recursive (i.e. it calls a function which
                       then calls it recursively) that's fine - the recursive
                       expansion will be stopped by the other function. *)
                let            
                    (* The environment for the expansion of this procedure
                       is the table for local declarations and the original
                       environment in which the function was declared for
                       non-locals. *)
                    fun lookupDec ({ addr=0, ...}, _, 0) =
                       (* Recursive reference - shouldn't happen. *)
                       raise InternalError ("lookupDec: Inline function recurses " ^ lambdaName)
                    |   lookupDec ({ addr=index, level, ...}, depth, 0) =
                         let (* On this level. *)
                            val optVal =
                                if index > 0
                                then localVec sub index (* locals *)
                                else Vector.sub(frozenParams, ~index) (* parameters *)
                            val value =
                                case optVal of
                                    SOME value => value
                                |   NONE => raise InternalError(
                                                    concat["Missing value address=", Int.toString index,
                                                           " level= ", Int.toString level, " while expanding ", lambdaName])
                         in
                           changeLevel value (depth - nestingOfThisProcedure)
                         end
                    |  lookupDec (ptr, depth, levels) =
                         (optEnviron funct) (ptr, depth, levels - 1);

                 in
                     optimiseProc 
                      {pt=lambdaBody,
                       lookupNewAddr=lookupLocalNewAddr,
                       lookupOldAddr=lookupDec, 
                       enterDec=setTabForInline,
                       enterNewDec=setNewTabForInline,
                       nestingOfThisProcedure=nestingOfThisProcedure,
                       spval=spval,
                       earlyInline=earlyInline orelse earlyEval,
                       evaluate=evaluate,
                       tailCallEntry=tailCall,
                       recursiveExpansions=recursiveExpansions,
                       maxInlineSize=maxInlineSize}
                  end

                else (* It's a "small" function. *)
                let
                (* Now load the procedure body itself.  We first process it assuming
                   that we won't need to treat any of the arguments specially.  If
                   we find that we generate a Loop instruction somewhere we have
                   to make sure that any arguments we change in the course of the
                   loop are taken out.  For example:
                   fun count'(n, []) = n | count' (n, _::t) = count'(n+1, t);
                   fun count l = count'(0, l).
                   In this case we would start by expanding count' using 0 for n
                   throughout, since it's a constant.  When we find the recursive
                   call in which n becomes n+1 we find we have to take n out of the
                   loop and treat it as a variable.
                   We don't need to do this if the argument is passed through unchanged
                   e.g. fun foldl f b [] = b  | foldl f b (x::y) = foldl f (f(x, b)) y;
                   where the same value for f is used everywhere and by treating it
                   specially we can expand its call. 
                   This two-pass (it will normally be two-pass but could be more) approach
                   allows us to optimise cases where we have a recursive function which
                   happens to be non-recursive with particular constant values of the
                   arguments.
                   e.g. if x = nil ... generates a general recursive function for
                   equality on lists but because of the nil argument this optimises
                   down to a simple test. *)
                (*
                   I'm now extending this to the general recursive case not just tail
                   recursion.  If we discover a recursive call while processing the
                   function we turn this expansion into a function call and give up
                   trying to inline it.  Instead we create a special-purpose function
                   for this call but with only the arguments that change as a
                   result of the recursive calls actually passed as arguments.  Other
                   arguments can be inserted inline in function.
                   e.g. fun map f [] = [] | map f (a::b) = f a :: map f b
                   where when we map a function over a list we compile a specialised
                   mapping function with the actual value of f inserted in it.
                *)
                    val needsBeginLoop = ref false
                    val needsRecursiveCall = ref false
                    val argModificationVec = Array.array(nArgs, false);
                    (* Create addresses for the new variables for modified arguments.
                       If newdecl created a variable we might be able to reuse that
                       but it's easier to create new ones. *)
                    val argBaseAddr = ! spval; val _ = spval := argBaseAddr + nArgs
    
                    (* filterArgs is called whenever a recursive call is made to
                       this function. *)
                    fun filterArgs (argList: (codetree * argumentType) list, isTail, depth) =
                    let
                        fun filterArgs' 0 [] = []
                          | filterArgs' _ [] =
                                raise InternalError "filterArgs': wrong number of args"
                          | filterArgs' n ((arg, argType) :: rest) =
                            let
                                (* Is this simply passing the original argument value?  *)
                                val original = valOf (Vector.sub(frozenParams, n))
                                val unChanged =
                                    case (arg, optGeneral original) of
                                        (Constnt w, Constnt w') =>
                                            (* These may well be functions so don't use
                                               structure equality. *)
                                            wordEq(w, w')
                                    | (Extract {addr=aA, level=aL, fpRel=aFp, ...},
                                       Extract {addr=bA, level=bL, fpRel=bFp, ...}) =>
                                        aA = bA andalso aFp = bFp andalso
                                        aL = bL+depth-nestingOfThisProcedure
                                    | _ => false

                            in
                                if unChanged
                                then ()
                                else Array.update(argModificationVec, n-1, true);
                                (* If any recursive call has changed it we need
                                   to include this argument even if it didn't
                                   change on this particular call. *)
                                if Array.sub(argModificationVec, n-1)
                                then (arg, argType) :: filterArgs' (n-1) rest
                                else (* Not modified *) filterArgs' (n-1) rest
                            end
                    in
                        needsBeginLoop := true; (* Indicate we generated a Loop instr. *)
                        (* If this isn't tail recursion we need a full call. *)
                        if isTail then () else needsRecursiveCall := true;
                        (* If we have a recursive inline function containing a
                           local recursive inline function which calls the outer
                           function
                           (e.g. fun f a b = .... map (f a) ....) we may process
                           the body of the inner function twice, once as a lambda
                           and once when we attempt to expand it inline.  That
                           means we will process the recursive call to the outer
                           function twice.  The first call may filter out redundant
                           arguments (e.g. "a" in the above example).  *)
                        if List.length argList <> nArgs
                        then argList
                        else filterArgs' nArgs argList
                    end

                    (* See how many arguments changed. *)
                    fun countSet() =
                    let
                        fun countSet' n 0 = n
                          | countSet' n i =
                                if Array.sub(argModificationVec, i-1) then countSet' (n+1) (i-1)
                                else countSet' n (i-1)
                    in
                        countSet' 0 nArgs
                    end

                    fun checkRecursiveCalls lastModCount =
                    (* We've found at least one non-tail recursive call so we're
                       going to have to generate this function as a function and
                       a call to that function.  However we may well gain by inserting
                       in line arguments which don't change as a result of recursion. *)
                    let
                        val nesting = nestingOfThisProcedure + 1
                        (* Find the parameter we're actually going to use. *)
                        fun getParamNo n =
                            if n = 0 then 0
                            else if Array.sub(argModificationVec, ~n -1)
                            then getParamNo (n+1) - 1
                            else getParamNo (n+1)

                        fun prev ({ addr=0, ...}, depth, 0) =
                             (* Recursive reference.  We're going to generate this
                                as a function so return a reference to the closure.
                                I've ensured that we pass the appropriate value for
                                recCall here although I don't know if it's necessary. *)
                             optVal {
                                general = mkGenLoad (0, depth - nesting, false, false),
                                (* This is a bit of a mess.  We need a non-nil value for
                                   special here in order to pass recCall correctly
                                   because optVal filters it otherwise. *)
                                special = (*optSpecial funct *)
                                    mkGenLoad (0, depth - nesting, false, false),
                                decs = [],
                                recCall = foptRec,
                                environ = errorEnv
                                }
                        |   prev ({ addr=index, ...}, depth, 0) =
                             if index > 0  (* locals *)
                             then changeLevel(valOf (localVec sub index)) (depth - nesting)
                             else (* index < 0 - parameters *)
                                if Array.sub(argModificationVec, ~index -1)
                             then (* This argument has changed - find the corresponding
                                     actual argument. *)
                                simpleOptVal(mkLoad(getParamNo index, depth-nesting))
                             else (* Unchanged - get the entry from the table, converting
                                     the level because it's in the surrounding scope. *)
                                changeLevel (valOf (Vector.sub(frozenParams, ~index))) (depth-nesting+1)
                        |  prev (ptr, depth, levels) =
                                (optEnviron funct) (ptr, depth, levels - 1);

                        val newAddrTab = stretchArray (initTrans, NONE);

                        (* localNewAddr is used as the environment of inline functions within
                           the function we're processing. *)
                        fun localNewAddr ({ addr=index, ...}, depth, 0) =
                          if index > 0 
                          then case newAddrTab sub index of
                                NONE => (* Return the original entry if it's not there. *)
                                    simpleOptVal(
                                        mkGenLoad (index, depth - nesting, true, false))
                            |   SOME v => changeLevel v (depth - nesting) 
                          else simpleOptVal (mkGenLoad (index, depth - nesting, index <> 0, false))
                        | localNewAddr (ptr, depth, levels) =
                            lookupNewAddr (ptr, depth, levels - 1);
            
                        fun setNewTab (addr, v) = update (newAddrTab, addr, SOME v)

                        fun setTab (index, v) =
                          (
                          case optGeneral v of
                              Extract{addr, ...} =>
                                if addr <= 0 then () else setNewTab(addr, v)
                            |   _ => ();
                          update (localVec, index, SOME v)
                          )

                        val newAddressAllocator = ref 1 (* Starts at one. *)

                        val copiedBody =
                             optimiseProc 
                              {pt=lambdaBody,
                               lookupNewAddr=localNewAddr,
                               lookupOldAddr=prev, 
                               enterDec=setTab,
                               enterNewDec=setNewTab,
                               nestingOfThisProcedure=nesting,
                               spval=newAddressAllocator,
                               earlyInline=earlyInline orelse earlyEval,
                               evaluate=evaluate,
                               tailCallEntry=NONE, (* Don't generate loop instructions. *)
                               recursiveExpansions=(filterArgs, foptRec) :: recursiveExpansions,
                               maxInlineSize=maxInlineSize}

                        val newModCount = countSet()
                    in
                        if newModCount > lastModCount
                        then (* We have some (more) arguments to include. *)
                            checkRecursiveCalls newModCount
                        else
                        let
                            fun filterArgTypes([], _) = []
                            |   filterArgTypes(hd:: tl, n) =
                                    if Array.sub(argModificationVec, n-1)
                                    then hd :: filterArgTypes(tl, n-1)
                                    else filterArgTypes(tl, n-1)
                            val newArgList = filterArgTypes(argTypes, nArgs)
                        in
                            optVal {
                                general = Lambda {
                                    body = getGeneral copiedBody,
                                    isInline = NonInline,
                                    name = lambdaName,
                                    closure = [],
                                    argTypes = newArgList, resultType=resultType,
                                    level = nestingOfThisProcedure + 1,
                                    closureRefs = 0,
                                    localCount = ! newAddressAllocator + 1,
                                    makeClosure = false, argLifetimes = [] },
                                special = CodeNil,
                                decs = [],
                                recCall = ref false,
                                environ = localNewAddr
                            }
                        end
                    end

                    fun checkForLoopInstrs lastModCount =
                    (* Called initially or while we only have tail recursive
                       calls.  We can inline the function. *)
                    let
                        (* There's still something strange going on where we end up with
                           a BeginLoop that doesn't contain any Loop.  See regression
                           test Succeed/Test108.ML. *)
                        val _ = needsBeginLoop := false;
                        fun prev ({ addr=index, ...}, depth, 0) : optVal =
                         let (* On this level. *)
                           val optVal =
                             if index = 0
                             (* Recursive reference - return the copied function after removing
                                the declarations.  These will be put on in the surrounding
                                scope.  We can't at this stage tell whether it's a call or
                                some other recursive use (e.g. passing it as an argument) and
                                it could be that it's an argument to an inline function which
                                actually calls it.  Since we include the original optRec value
                                it can be sorted out later. *)
                             then stripDecs funct
                             else if index > 0
                             then valOf (localVec sub index)  (* locals *)
                             else (* index < 0 *) if Array.sub(argModificationVec, ~index-1)
                             then (* This argument changes - must use a variable even if
                                     the original argument was a constant. *)
                                simpleOptVal(mkLoad(argBaseAddr + nArgs + index, 0))
                             else valOf (Vector.sub(frozenParams, ~index)) (* parameters *)
                         in
                           changeLevel optVal (depth - nestingOfThisProcedure)
                         end
                        | prev (ptr, depth, levels) : optVal =
                            (* On another level. *)
                            (optEnviron funct) (ptr, depth, levels - 1);

                        val copiedBody =
                             optimiseProc 
                              {pt=lambdaBody,
                               lookupNewAddr=lookupLocalNewAddr,
                               lookupOldAddr=prev, 
                               enterDec=setTabForInline,
                               enterNewDec=setNewTabForInline,
                               nestingOfThisProcedure=nestingOfThisProcedure,
                               spval=spval,
                               earlyInline=earlyInline orelse earlyEval,
                               evaluate=evaluate,
                               tailCallEntry=SOME foptRec,
                               recursiveExpansions=(filterArgs, foptRec) :: recursiveExpansions,
                               maxInlineSize=maxInlineSize}
                    in
                        if ! needsRecursiveCall
                        then (* We need a fully recursive call. *)
                            checkRecursiveCalls (countSet())
                        else if ! needsBeginLoop 
                        then (* We've found at least one recursive call which changes its
                                argument value. *)
                        let
                            val newModCount = countSet()
                        in
                            if newModCount > lastModCount
                            then checkForLoopInstrs newModCount
                            else copiedBody
                        end
                        else copiedBody
                    end
        
                    val procBody = checkForLoopInstrs 0

                    (* If we need to make the declarations put them in at the
                       beginning of the loop. *)
                    fun makeDecs(0, [], _) = []
                      | makeDecs(n, (_, typ):: rest, isCall) =
                            if not (Array.sub(argModificationVec, n-1))
                            then makeDecs (n-1, rest, isCall)
                            else
                            let
                                val argVal = getGeneral(valOf (Vector.sub(frozenParams, n)))
                                val argDec =
                                (* If we are calling a function we just put the
                                   argument values in. *)
                                    if isCall
                                    then argVal
                                    else mkDec(argBaseAddr+nArgs-n, argVal)
                            in
                                (argDec, typ) :: makeDecs (n-1, rest, isCall)
                            end
                     |  makeDecs _ = raise InternalError "Unequal lengths"
                    fun mkDecs isCall = makeDecs(nArgs, argList, isCall)
                in
                    if ! needsRecursiveCall
                    then (* We need to put in a call to this function. *)
                        let
                            (* Put the function into the declarations. *)
                            val addr = ! spval
                        in
                            spval := addr + 1;
                            optVal{
                                general =
                                    Eval {function = mkLoad(addr, 0), argList = mkDecs true,
                                          earlyEval = false, resultType=resultType},
                                special = CodeNil,
                                decs = [mkDec(addr, getGeneral procBody)],
                                recCall = ref false,
                                environ = lookupNewAddr
                            }
                        end
                    else if ! needsBeginLoop
                    then simpleOptVal(BeginLoop{loop=getGeneral procBody, arguments=mkDecs false})
                    else if Array.exists(fn x => x) argModificationVec
                    then (* We could have reset needsBeginLoop to false and ended up not needing a loop.
                            Ifwe have declarations that we made earlier we have to include them. *)
                        optVal{general = optGeneral procBody, special = optSpecial procBody,
                               environ = optEnviron procBody, recCall = optRec procBody,
                               decs = List.map #1 (mkDecs false) @ optDecs procBody}
                    else procBody
                end           
          in
            StretchArray.freeze localVec;
            StretchArray.freeze localNewVec;
           (* The result is the result of the body of the inline procedure. *)
           (* The declarations needed for the inline procedure, the         *)
           (* declarations used to load the arguments and the declarations  *)
           (* in the expanded procedure are all concatenated together. We   *)
           (* do not attempt to evaluate "early inline" procedures. Instead *)
           (* we try to ensure that all procedures inside are evaluated     *)
           (*"early". *)
          optVal 
          {
            general = optGeneral copiedBody,
            special = optSpecial copiedBody,
            environ = optEnviron copiedBody,
            decs    = optDecs funct @ (copiedArgs @ optDecs copiedBody),
            recCall = optRec copiedBody
          }
          end
                
          | _ => notInlineCall NONE (* Not a Lambda and not recursive. *)
        end (* Eval { } *)
        
     |  optimise (Extract(ext as {level, ...}), _) =
            lookupOldAddr (ext, nestingOfThisProcedure, level)

     |  optimise (original as Lambda({isInline=OnlyInline, ...}), _) =
            (* Used only for functors.  Leave unchanged. *)
            optVal 
                {
                  general = CodeZero,
                  (* Changed from using CodeNil here to CodeZero.  This avoids a problem
                     which only surfaced with the changes to ML97 and the possibility of
                     mixing functor and value declarations in the same "program" (i.e.
                     top-level declarations with a terminating semicolon.
                     OnlyInline is used for functors which can only ever be called,
                     never passed as values, so the "general" value is not really
                     required.  It can though appear in the result tuple of the "program"
                     from which the (value) results of the program are extracted.

                     Further change: This previously returned the processed body but the effect of
                     that was to prevent small functions inside the functor from becoming
                     inline functions when the functor was applied.  Instead we just return
                     the original code. *)
                  special = original,
                  environ = lookupOldAddr, (* Old addresses with unprocessed body. *)
                  decs    = [],
                  recCall = ref false
                }

     |  optimise (original as Lambda({body=lambdaBody, isInline=lambdaInline, name=lambdaName,
                          argTypes, resultType, ...}), _) =
        let
          (* The nesting of this new procedure is the current nesting level
             plus one. Normally this will be the same as lambda.level except
             when we have a procedure inside an inline procedure. *)
          val nesting = nestingOfThisProcedure + 1;
          
          (* A new table for the new procedure. *)
          val oldAddrTab = stretchArray (initTrans, NONE);
          val newAddrTab = stretchArray (initTrans, NONE);

          fun localOldAddr ({ addr=index, level, ...}, depth, 0) =
              (* local declaration or argument. *)
              if index > 0 
              (* Local declaration. *)
              then
                case  oldAddrTab sub index of
                    SOME v => changeLevel v (depth - nesting)
                |   NONE => raise InternalError(
                                    concat["localOldAddr: Not found. Addr=", Int.toString index,
                                           " Level=", Int.toString level])
              (* Argument or closure. *)
              else simpleOptVal (mkGenLoad (index, depth - nesting, index <> 0, false))
            | localOldAddr (ptr, depth, levels) = lookupOldAddr (ptr, depth, levels - 1);

          (* localNewAddr is used as the environment of inline functions within
             the function we're processing.  All the entries in this table will
             have their "general" entries as simply Extract entries with the
             original address.  Their "special" entries may be different. The
             only entries in the table will be those which correspond to
             declarations in the original code so there may be new declarations
             which aren't in the table. *)
          fun localNewAddr ({ addr=index, ...}, depth, 0) =
              if index > 0 
              then case newAddrTab sub index of
                    NONE => (* Return the original entry if it's not there. *)
                        simpleOptVal(mkGenLoad (index, depth - nesting, true, false))
                |   SOME v => changeLevel v (depth - nesting) 
              else simpleOptVal (mkGenLoad (index, depth - nesting, index <> 0, false))
            | localNewAddr (ptr, depth, levels) = lookupNewAddr (ptr, depth, levels - 1);

          fun setNewTab (addr, v) = update (newAddrTab, addr, SOME v)

          fun setTab (index, v) =
            (
            case optGeneral v of
                Extract{addr, ...} =>
                    if addr <= 0 then () else setNewTab(addr, v)
            |   _ => ();
            update (oldAddrTab, index, SOME v)
            )

          val newAddressAllocator = ref 1

          val newCode =
             optimiseProc 
               {pt=lambdaBody,
                lookupNewAddr=localNewAddr,
                lookupOldAddr=localOldAddr, 
                enterDec=setTab,
                enterNewDec=setNewTab,
                nestingOfThisProcedure=nesting,
                spval=newAddressAllocator,
                earlyInline=false,
                evaluate=evaluate,
                tailCallEntry=NONE,
                recursiveExpansions=recursiveExpansions,
                maxInlineSize=maxInlineSize}

          (* nonLocals - a list of the non-local references made by this
             function.  If this is empty the function can be code-generated
             immediately and returned as a constant.  If it is non-empty it
             is set as the closure for the function.  This is then used
             when processing mutually recursive functions to find the
             dependencies. *)
             
          val nonLocals = ref nil;
          fun addNonLocal({addr, level, fpRel, ...}, depth) =
          let
             (* The level will be correct relative to the use, which may be
                in an inner function.  We want the level relative to the
                scope in which this function is declared. *)
             val correctedLevel = level - (depth - nestingOfThisProcedure)
             fun findNonLocal(Extract{addr=addr', level=level', fpRel=fpRel', ...}) =
                    addr = addr' andalso correctedLevel = level' andalso fpRel=fpRel'
              |  findNonLocal _ = raise InternalError "findNonLocal: not Extract"
          in
             if List.exists findNonLocal (!nonLocals)
             then () (* Already there. *)
             else nonLocals := mkGenLoad(addr, correctedLevel, fpRel, false) :: ! nonLocals
          end

          fun checkRecursion(ext as {fpRel=oldfpRel, ...}, levels, depth) =
              case optGeneral(lookupNewAddr (ext, depth, levels)) of
                 (res as Extract(ext as {addr=0, fpRel=false, ...})) =>
                     (
                     (* If this is just a recursive call it doesn't count
                        as a non-local reference.  This only happens if
                        we turned a reference to a local into a recursive
                        reference (i.e. fpRel was previously true). *)
                     if levels = 0 andalso oldfpRel
                     then ()
                     else addNonLocal(ext, depth);
                     res
                     )
              |  res as Extract ext =>
                    (
                     addNonLocal(ext, depth);
                     res
                    )

              |  res => res (* We may have a constant in this table. *)

          val cleanedBody =
            cleanProc(getGeneral newCode, checkRecursion, nesting,
                      Array.array (! newAddressAllocator + 1, false))

          val resultCode =
            case lambdaInline of
                MaybeInline => (* Explicitly inlined functions. *)
                    (* We return the processed version of the function as
                       the general value but the unprocessed version as
                       the special value. *)
                    optVal 
                    {
                      general =
                        Lambda 
                          {
                           body          = cleanedBody,
                           isInline      = MaybeInline,
                           name          = lambdaName,
                           closure       = !nonLocals, (* Only looked at in MutualDecs. *)
                           argTypes      = argTypes,
                           resultType    = resultType,
                           level         = nesting,
                           closureRefs   = 0,
                           localCount    = ! newAddressAllocator + 1,
                           makeClosure   = false, argLifetimes = []
                         },
                      special = original,
                      environ = lookupOldAddr, (* Old addresses with unprocessed body. *)
                      decs    = [],
                      recCall = ref false (* *** REF HOTSPOT; Contributes many refs to the environment. *)
                    }

            |   _ => (* "Normal" function.  If the function is small we mark it as
                        inlineable.  If the body has no free variables we compile it
                        now so that we can propagate the resulting constant, otherwise
                        we return the processed body.  We return the processed body as
                        the special value so that it can be inlined.  We do this even
                        in the case where the function isn't small because it is just
                        possible we're going to apply the function immediately and in
                        that case it's worth inlining it anyway. *)
              let
                    val inlineType =
                        if lambdaInline = NonInline
                        then if (* Is it small? *) codeSize(cleanedBody, true) < maxInlineSize
                        then SmallFunction else NonInline
                        else lambdaInline

                  val copiedLambda =
                    Lambda 
                      {
                       body          = cleanedBody,
                       isInline      = inlineType,
                       name          = lambdaName,
                       closure       = !nonLocals, (* Only looked at in MutualDecs. *)
                       argTypes      = argTypes,
                       resultType    = resultType,
                       level         = nesting,
                       closureRefs   = 0,
                       localCount    = ! newAddressAllocator + 1,
                       makeClosure   = false, argLifetimes = []
                     };
                 val general = 
                   (* If this has no free variables we can code-generate it now. *)
                   if null (!nonLocals)
                   then evaluate(copiedLambda, !spval+1)
                   else copiedLambda
              in
                  optVal 
                    {
                      general = general,
                      special =
                        Lambda 
                          {
                           body          = cleanedBody,
                           isInline      = inlineType,
                           name          = lambdaName,
                           closure       = [],
                           argTypes      = argTypes,
                           resultType    = resultType,
                           level         = nesting,
                           closureRefs   = 0,
                           localCount    = ! newAddressAllocator + 1,
                           makeClosure   = false, argLifetimes = []
                         },
                      environ = lookupNewAddr,
                      decs    = [],
                      recCall = ref false (* *** REF HOTSPOT; Contributes many refs to the environment. *)
                    }
            end
        in
            StretchArray.freeze oldAddrTab;
            StretchArray.freeze newAddrTab;
            resultCode
        end (* Lambda{...} *)
           
     |  optimise (pt as Constnt _, _) =
            simpleOptVal pt (* Return the original constant. *)
           
     |  optimise (BeginLoop{loop=body, arguments=args, ...}, tailCall) =
        let
             (* We could try extracting redundant loop variables but for
                the time being we just see whether we actually need a loop
                or not.  This is needed if we have already constructed a loop
                from a recursive inline function and then expand it in another
                function.  If some of the loop variables are now constants we may
                optimise away the loop altogether. e.g. equality for lists where
                we actually have   if x = nil then ... *)
             val loops = ref false
             fun filterArgs (a, _, _) = (loops := true; a)

             val foptRec = ref false
             (* First process as though it was not a BeginLoop but just a
                set of declarations followed by an expression. *)
             val _ =
                optimiseProc 
                  {pt=mkEnv(List.map #1 args @ [body]), lookupNewAddr=lookupNewAddr, lookupOldAddr=lookupOldAddr,
                   enterDec=enterDec, enterNewDec=enterNewDec, nestingOfThisProcedure=nestingOfThisProcedure,
                   spval=spval, earlyInline=earlyInline, evaluate=evaluate, tailCallEntry=SOME foptRec,
                   recursiveExpansions=(filterArgs, foptRec) :: recursiveExpansions,
                   maxInlineSize=maxInlineSize}
        in
            if not (! loops)
            then (* The Loop instructions have been optimised away.  Since there's
                    no BeginLoop we can reprocess it with the surrounding
                    tail recursion. *)
                optimise(mkEnv(List.map #1 args @ [body]), tailCall)
            else (* It loops - have to reprocess. *)
            let
                (* The arguments to the functions are Declar entries but they
                   must not be optimised. *)
                fun declArg(Declar{addr, value, ...}, typ) =
                    let
                        val optVal = optimise(value, NONE)
                        val decSpval = ! spval
                        val _ = spval := decSpval + 1
                        val optV = simpleOptVal(mkLoad (decSpval, 0))
                    in
                        enterDec(addr, optV);
                        (mkDec(decSpval, getGeneral optVal), typ)
                    end
                 |  declArg _ = raise InternalError "declArg: not Declar"
                 val declArgs = map declArg args
                 val beginBody =
                    optimiseProc 
                      {pt=body, lookupNewAddr=lookupNewAddr, lookupOldAddr=lookupOldAddr, enterDec=enterDec,
                       enterNewDec=enterNewDec, nestingOfThisProcedure=nestingOfThisProcedure, spval=spval,
                       earlyInline=earlyInline, evaluate=evaluate, tailCallEntry=SOME foptRec,
                       recursiveExpansions=(filterArgs, foptRec) :: recursiveExpansions,
                       maxInlineSize=maxInlineSize}
            in
                simpleOptVal (BeginLoop {loop=getGeneral beginBody, arguments=declArgs})
            end
        end

     |  optimise (Loop args, tailCall) =
        (
            (* The Loop instruction should be at the tail of the
               corresponding BeginLoop. *)
            case (tailCall, recursiveExpansions) of
                (SOME fopt, (filterArgs, fopt') :: _) =>
                    let
                        fun gen(c, t) = (general c, t)
                    in
                        if fopt <> fopt'
                        then raise InternalError "Loop: mismatched BeginLoop"
                        else simpleOptVal (Loop ((filterArgs(map gen args, true, nestingOfThisProcedure))))
                    end
            |   _ => raise InternalError "Loop: not at tail of BeginLoop"
        )
          
     |  optimise (Raise x, _) = simpleOptVal (Raise (general x))
         
     |  optimise (Cond(condTest, condThen, condElse), tailCall) =
        let
          val insFirst = general condTest;
        in
          (* If the condition is a constant we need only
             return the appropriate arm. *)
          case insFirst of
            Constnt testResult =>
            if wordEq (testResult, False) (* false - return else-part *)
            then 
          (* if false then x else y == y *)
              if isCodeNil condElse (* May be nil. (Pattern-matching) *)
              then simpleOptVal (mkEnv [])
              else optimise(condElse, tailCall)
          (* if true then x else y == x *)
            else optimise(condThen, tailCall)  (* return then-part *)
            
           | _ =>
          let
            (* Perhaps the "if" is really a simpler expression?
               Unfortunately, we don't know whether we're returning
               a boolean result here so we can't optimise to
               andalso/orelse but we can at least look for the
               case where both results are constants. *)
            val insSecond = optimise(condThen, tailCall)
            val insThird  = optimise(condElse, tailCall)

            (* If we have tuples on both arms we can probably combine them. *)
            fun combineTuples(containerAddr, thenAddr, elseAddr, thenRec, elseRec, size) =
            let
                val thenDecs = optDecs insSecond and elseDecs = optDecs insThird

                fun replaceContainerDec([], _) =
                    raise InternalError "replaceContainerDec"
                 |  replaceContainerDec((hd as Declar{addr, ...})::tl, ad)=
                        if addr = ad
                        then (* Found the declaration. If we are using this
                                container address we remove this declaration.
                                If we have containers on both branches we
                                need to make them both point to the same
                                container. *)
                            if addr = containerAddr
                            then tl
                            else mkDec(addr, mkLoad(containerAddr, 0)) :: tl
                        else hd :: replaceContainerDec(tl, ad) 
                | replaceContainerDec(hd :: tl, ad) =
                    hd :: replaceContainerDec(tl, ad)

                fun createBranch(recEntries, decEntries, cAddr) =
                    case cAddr of
                        SOME ad => (* We have a container on that branch ... *)
                           wrapEnv(replaceContainerDec(decEntries, ad))
                    |   NONE => 
                            wrapEnv(decEntries @
                                [mkSetContainer(
                                    mkLoad(containerAddr, 0), Recconstr recEntries,
                                    size)])

                val thenPart = createBranch(thenRec, thenDecs, thenAddr)
                and elsePart = createBranch(elseRec, elseDecs, elseAddr)
                (* The result is a block which declares the container, side-effects it
                   in the "if" and makes a tuple from the result.  If we're lucky
                   the resulting tuple will be optimised away. *)
                (* This code is the same as that used to optimise TupleFromContainer
                   and is designed to allow us to optimise away the tuple creation
                   if we use the individual fields. *)
                val baseAddr = !spval
                val _ = spval := baseAddr + size
                val specialDecs =
                    List.tabulate(size,
                        fn n => mkDec(n+baseAddr, mkInd(n, mkLoad(containerAddr, 0))))
                val specialEntries = List.tabulate(size, fn n => mkLoad(n+baseAddr, 0))
                fun env (l:loadForm, depth, _) : optVal =
                    changeLevel (simpleOptVal(Extract l)) (depth - nestingOfThisProcedure)
            in
                optVal 
                    {
                      general = TupleFromContainer(mkLoad(containerAddr, 0), size),
                      special = Recconstr specialEntries,
                      environ = env,
                      decs    =
                          mkDec(containerAddr, Container size) ::
                          mkIf(insFirst, thenPart, elsePart) :: specialDecs,
                      recCall = ref false
                    }
            end (* combineTuples *)
          in
            case (optGeneral insSecond, optDecs insSecond,
                  optGeneral insThird, optDecs insThird) of
                (second as Constnt c2, [], third as Constnt c3, []) =>
              (* if x then y else y == (x; y) *)
              if wordEq (c2, c3)
              then if sideEffectFree insFirst
                  then insSecond
                  else
                  (* Must put insFirst in decs, so it gets executed *) 
                  optVal 
                    {
                      general = second,
                      special = CodeNil,
                      environ = errorEnv,
                      decs    = [insFirst],
                      recCall = ref false
                    }
                  
              (* if x then true else false == x *)
              else if wordEq (c2, True) andalso wordEq (c3, False)
                then simpleOptVal insFirst
              
              (* if x then false else y == not x *)
              else if wordEq (c2, False) andalso wordEq (c3, True)
                then simpleOptVal (mkNot insFirst)
              
              else (* can't optimise *)
                simpleOptVal (mkIf (insFirst, second, third))

           | (Recconstr thenRec, _, Recconstr elseRec, _) =>
                (* Both tuples - are they the same size?  They may not be if they
                   are actually datatypes. *)
                if List.length thenRec = List.length elseRec
                then (* We can transform this into an operation which creates space
                        on the stack, side-effects it and then picks up the result
                        from it. *)
                let
                    val size = List.length thenRec (* = List.length elseRec *)
                    (* Create a new address for the container. *)
                    val containerAddr = let val ad = !spval in spval := ad + 1; ad end
                in
                    combineTuples(containerAddr, NONE, NONE, thenRec, elseRec, size)
                end
                else (* Different sizes - use default. *)
                   simpleOptVal (mkIf (insFirst, getGeneral insSecond, getGeneral insThird))

           | (TupleFromContainer(Extract{addr=thenAddr,level=0,fpRel=true, ...}, thenSize), _,
              TupleFromContainer(Extract{addr=elseAddr,level=0,fpRel=true, ...}, elseSize), _) =>
                (* Have both been converted already.  If we are returning a tuple from
                   a container the container must be declared locally. *)
                if thenSize = elseSize
                then (* We can combine the containers.  We can't if these are actually
                        datatypes in which case they could be different sizes. *)
                let
                    (* If we have already transformed this we will have a
                       declaration of a container somewhere in the list. *)
                    (* Use the address which has already been allocated for the else part.
                       That makes it easier for the subsequent pass to convert this into
                       a "case" if appropriate. *)
                    val containerAddr = elseAddr
                in
                    combineTuples(containerAddr, SOME thenAddr, SOME elseAddr, [], [], thenSize)
                end
                else (* Different sizes - use default. *)
                   simpleOptVal (mkIf (insFirst, getGeneral insSecond, getGeneral insThird))

           | (TupleFromContainer(Extract{addr=thenAddr,level=0,fpRel=true, ...}, thenSize), _,
              Recconstr elseRec, _) =>
                (* The then-part has already been converted *)
                if thenSize = List.length elseRec
                then combineTuples(thenAddr, SOME thenAddr, NONE, [], elseRec, thenSize)
                else (* Different sizes - use default. *)
                   simpleOptVal (mkIf (insFirst, getGeneral insSecond, getGeneral insThird))

           | (Recconstr thenRec, _,
              TupleFromContainer(Extract{addr=elseAddr,level=0,fpRel=true, ...}, elseSize), _) =>
                (* The else-part has already been converted *)
                if elseSize = List.length thenRec
                then
                    combineTuples(elseAddr, NONE, SOME elseAddr, thenRec, [], elseSize)
                else (* Different sizes - use default. *)
                   simpleOptVal (mkIf (insFirst, getGeneral insSecond, getGeneral insThird))

           | _ => (* Not constants or records. *)
            simpleOptVal (mkIf (insFirst, getGeneral insSecond, getGeneral insThird))
          end
        end (* isCond pt *)
         
     |  optimise (Newenv envDecs, tailCall) =
        let (* Process the body. *)
          (* Recurses down the list of declarations and expressions processing
             each, and then reconstructs the list on the way back. *)

          (* Only if we have an empty block or a block containing only
             declarations i.e. a declaration is used to discard the result
             of a function and only perform its side-effects. *)
          fun copyDeclarations []  = simpleOptVal (mkEnv [])
            | copyDeclarations (Declar{addr=caddr, value, ...} :: vs) = 
              let
                (* Add the declaration to the table. *)
                val dec =
                  newDecl (enterDec, optimise(value, NONE), caddr, true);
                  
                (* Deal with the rest of the block. *)
                val rest = copyDeclarations vs;
             in
                case dec of
                  [] => rest
                | _  => (* Must put these declarations onto the list. *)
                  optVal 
                    {
                      general = optGeneral rest,
                      special = optSpecial rest,
                      environ = optEnviron rest,
                      decs    = dec @ optDecs rest,
                      recCall = optRec rest
                    }
            end

            | copyDeclarations (MutualDecs mutualDecs :: vs) = 
            (* Mutually recursive declarations. Any of the declarations may
               refer to any of the others. They should all be lambdas.

               The front end generates functions with more than one argument
               (either curried or tupled) as pairs of mutually recursive
               functions.  The main function body takes its arguments on
               the stack (or in registers) and the auxiliary inline function,
               possibly nested, takes the tupled or curried arguments and
               calls it.  If the main function is recursive it will first
               call the inline function which is why the pair are mutually
               recursive.
               As far as possible we want to use the main function since that
               uses the least memory.  Specifically, if the function recurses
               we want the recursive call to pass all the arguments if it
               can.  We force the inline functions to be macros while
               processing the non-inline functions and then process the
               inlines. DCJM 23/1/01. *)
            let
                (* Split the inline and non-inline functions. *)
                val (inlines, nonInlines) =
                    List.partition (
                        fn Declar{value = Lambda{ isInline=MaybeInline, ...}, ... } => true | _ => false) mutualDecs
                    
                (* Go down the non-inline functions creating new addresses
                   for them and entering them in the table. *)
                val startAddr = !spval
                val addresses =
                    map (fn Declar{addr, ... } =>
                        let
                            val decSpval   = !spval;
                        in
                            enterDec (addr, simpleOptVal (mkLoad (decSpval, 0)));
                            spval := !spval + 1;
                            decSpval
                        end
                      | _ => raise InternalError "mutualDecs: not Declar")
                    nonInlines
                val endAddr = !spval
                (* We can now process the inline functions.  Since these
                   can't be directly recursive we don't need to do anything
                   special. *)
                val _ =
                    List.app (fn Declar{ value, addr, ... } =>
                                enterDec (addr, optimise(value, NONE))
                          | _ => raise InternalError "mutualDecs: not Declar")
                    inlines

                (* Next process the non-inlines.  We really want to be able to
                   compile the functions now if we can and get a constant for
                   the code address.  We can do that for functions which make
                   no non-local references or whose non-local references are
                   by means of constants.  For non-recursive declarations this
                   is easy since an earlier declaration cannot depend on a later
                   one but for mutually recursive declarations we don't know
                   the dependencies.
                   The simple case is where we have a function which does not
                   depend on anything and so can be code-generated in the Lambda
                   case.  Code-generating that may allow others to be code-generated.
                   Another case is where the functions depend on each other but not
                   on anything else.  We can compile them together but not
                   individually.  There are various versions of this second case.
                   The only one we consider here is if all the (non-constant)
                   functions are of that form in which case we process the
                   whole mutually-recursive declaration. *)
                val hasNonLocalReference = ref false

                fun checkClosure (Extract{addr, level=0, fpRel=true, ...}) =
                    if addr >= startAddr andalso addr < endAddr
                    then ()
                    else hasNonLocalReference := true
                |   checkClosure _ = hasNonLocalReference := true

                fun processNonInlines (Declar{ value = decVal, addr = decAddr, ... },
                                       decSpval, (decs, otherChanges)) =
                (* Have a look at the old entry to see if it's a constant. *)
                let
                    val oldEntry =
                        lookupOldAddr(
                                {addr=decAddr, level=0, fpRel=true, lastRef=false},
                                nestingOfThisProcedure, 0)
                in
                    case optGeneral oldEntry of
                        oldGen as Constnt _ =>
                            (mkDec (decSpval, oldGen) :: decs, otherChanges) (* It's already a constant - don't reprocess. *)
                    |   _ =>
                        let
                            (* Set this entry to create a recursive call if we load
                               the address while processing the function. The recursive
                               call may come about as a result of expanding an inline
                               function which then makes the recursive call. *)
                            local
                                val recursive = simpleOptVal (mkGenLoad (0, ~1, false, false))
                            in
                                val _ = enterDec(decAddr, recursive);
                                val _ = enterNewDec(decSpval, recursive)
                            end;
                       
                            (* Now copy this entry. *)
                            val ins  = optimise(decVal, NONE)

                            val gen  = optGeneral ins;
                            val spec = optSpecial ins;

                            (* The general value is either a reference to the
                               declaration or a constant if the function has just
                               been compiled into a code segment. *)
                            val isConstant = isConstnt gen
                            val optGen =
                                case gen of
                                    Constnt _ => gen
                                |   Lambda{closure, ...} => (
                                        List.app checkClosure closure;
                                        mkLoad (decSpval, 0)
                                    )
                                |   _ => raise InternalError "processNonInlines: Not a function";

                            (* Explicitly reset the entry in the new table. *)
                            val _  = enterNewDec(decSpval, simpleOptVal optGen);
              
                            (* If this is a small function we leave the special
                               value so it can be inserted inline.  Otherwise
                               we clear it. *)
                            val optSpec =
                                 case spec of
                                    Lambda{ isInline=NonInline, ...} => CodeNil
                                   | _ => optSpecial ins;
                            val nowInline =
                                not (isCodeNil optSpec) andalso isCodeNil(optSpecial oldEntry)
                            (* If this is now a constant or it is a small function when it
                               wasn't before we need to reprocess everything
                               which depends on it to try to get the constant inserted
                               everywhere it can be. *)
                      in
                              enterDec 
                                (decAddr,
                                 optVal 
                                    {
                                      general = optGen,
                                      special = optSpec,
                                      environ = optEnviron ins,
                                      decs    = optDecs ins, (* Should be nil. *)
                                      recCall = optRec ins
                                    });
                            (
                             mkDec (decSpval, gen) :: decs,
                             otherChanges orelse isConstant orelse nowInline
                            )
                      end
               end

              | processNonInlines _ =
                    raise InternalError "processNonInlines: not Declar"

              fun repeatProcess () =
              let
                  val (decs, haveChanged) =
                     (* Use foldr here to keep the result in the same order
                        in case we can compile them immediately below. *)
                     ListPair.foldr processNonInlines
                        ([], false) (nonInlines, addresses);
              in
                 if haveChanged
                 then repeatProcess ()
                 else decs
              end

              val decs = repeatProcess ()

              val allAreConstants =
                List.foldl
                    (fn(Declar{value=Constnt _, ...}, others) => others
                      | _ => false) true decs

              (* If hasNonLocalReference is still false we can code-generate
                 the mutual declarations. *)
              val decs =
                 if ! hasNonLocalReference orelse allAreConstants
                 then decs
                 else
                    let
                        (* Create a tuple of Extract entries to get the result. *)
                        val extracts =
                            List.map (
                                fn (Declar{addr, ...}) => mkLoad(addr, 0)
                                 | _ => raise InternalError "extracts: not Declar")
                                decs
                        val code = mkEnv[mkMutualDecs decs, mkTuple extracts]
                        (* Code generate it. *)
                        val results = evaluate(code, !spval+1)

                        fun reprocessDec(Declar{addr=decAddr, ...}, decSpval, (offset, others)) =
                        let
                            val oldEntry =
                                lookupOldAddr(
                                        {addr=decAddr, level=0, fpRel=true, lastRef=false},
                                        nestingOfThisProcedure, 0)
                        in
                            let
                                val newConstant = findEntryInBlock results offset
                            in
                                (* Replace the entry by an entry with a constant. *)
                                enterNewDec(decSpval, simpleOptVal newConstant);
                                enterDec 
                                    (decAddr,
                                     optVal 
                                        {
                                          general = newConstant,
                                          special = optSpecial oldEntry,
                                          environ = optEnviron oldEntry,
                                          decs    = optDecs oldEntry, (* Should be nil. *)
                                          recCall = optRec oldEntry
                                        });
                                (offset+1, mkDec(decSpval, newConstant) :: others)
                            end
                        end
                        |   reprocessDec _ = raise InternalError "reprocessDec: not Declar"
                        
                        val (_, newDecs) = ListPair.foldl reprocessDec (0, []) (nonInlines, addresses);
                    in
                        newDecs (* We've converted them all to constants. *)
                    end

              (* Deal with the rest of the block *)
              val rest = copyDeclarations vs

                (* Separate out the constants.  They don't need to be mutually declared and putting them earlier
                   may mean that they can be inserted into the code. *)
                val (constnts, nonConsts) =
                    List.partition(fn Declar{value=Constnt _, ...} => true | _ => false) decs

                val mutuals =
                    case nonConsts of
                        [] => []
                    |   [singleton] => [singleton]
                    |   multiple => [mkMutualDecs multiple]
            in
              (* and put these declarations onto the list. *)
              optVal
                {
                  general = optGeneral rest,
                  special = optSpecial rest,
                  environ = optEnviron rest,
                  decs    = constnts @ mutuals @ optDecs rest,
                  recCall = optRec rest
                }
            end
          
            | copyDeclarations [v] =
                (* Last expression. *) optimise(v, tailCall)

            | copyDeclarations (v :: vs) = 
            let (* Not a declaration - process this and the rest.*)
                val copiedNode = optimise(v, NONE);
                val rest = copyDeclarations vs;
            in  (* This must be a statement whose
                   result is ignored. Put it into the declaration list. *)
                optVal 
                    {
                      general = optGeneral rest,
                      special = optSpecial rest,
                      environ = optEnviron rest,
                      decs    = optDecs copiedNode @ 
                        (optGeneral copiedNode :: optDecs rest),
                      recCall = optRec rest
                    }
          end; (* copyDeclarations *)
        in
          copyDeclarations envDecs
        end (* isNewenv *)
          
    |   optimise (Recconstr entries, _) =
         (* The main reason for optimising record constructions is that they
            appear as tuples in ML. We try to ensure that loads from locally
            created tuples do not involve indirecting from the tuple but can
            get the value which was put into the tuple directly. If that is
            successful we may find that the tuple is never used directly so
            the use-count mechanism will ensure it is never created. *)
        let
            val newTab = Array.array(List.length entries+1, NONE)
            fun setTab (i, v) = Array.update (newTab, i, SOME v);
            (* The record construction is treated as a block of local
               declarations so that any expressions which might have side-effects
               are done exactly once. *)
            fun makeDecs []     _ = {decs = [], gen = [], spec = []}
            |   makeDecs (h::t) addr =
                let
                    (* Declare this value. If it is anything but a constant
                       there will be some code. *)
                    val newDecs = newDecl (setTab, optimise(h, NONE), addr, true)
                    val thisArg = valOf (Array.sub(newTab, addr)) (* Get the value back. *)
                    val {decs=restDecs, gen=genRest, spec=specRest} = makeDecs t (addr + 1)
                    val gen = optGeneral thisArg
                    (* If there's no "special" and the general is a constant use that for
                       the special entry otherwise load the original value. *)
                    val specArg =
                        case (optSpecial thisArg, gen) of
                            (CodeNil, Constnt _) => gen
                        |   _ => mkLoad (addr, 0)
                in
                    {gen = gen :: genRest, spec = specArg :: specRest, decs = newDecs @ restDecs }
                end
          
            val {gen, spec, decs} = makeDecs entries 1
            val newRec  = Recconstr gen
            (* Package up the declarations so we can extract the special values. *)
            val vec = Array.vector newTab
            fun env ({addr, ...}:loadForm, depth, _) : optVal =
                changeLevel (valOf (Vector.sub(vec, addr))) (depth - nestingOfThisProcedure)
        in
            optVal 
            {
                (* If all the general values are constants we can create the tuple now. *)
                general = if List.all isConstnt gen then makeConstVal newRec else newRec,
                special = Recconstr spec,
                environ = env,
                decs    = decs,
                recCall = ref false
            }
        end
          
      |  optimise (Indirect{ base, offset }, _) = (* Try to do the selection now if possible. *)
            doIndirection(optimise(base, NONE), offset)
       
      |  optimise (pt as Ldexc, _) =
            simpleOptVal pt (* just a constant so return it *)
        
      |  optimise (Handle { exp, handler }, tailCall) =
        simpleOptVal 
          (Handle {exp     = general exp,
                   handler = generalOptimise(handler, tailCall)}
          )

      |  optimise (c as Container _, _) = simpleOptVal c

      |  optimise (TupleFromContainer(container, size), _) =
        let
            (* If possible we want to optimise this away in the same way as
               a tuple made with Recconstr.  We have to be careful, though,
               that we have no references to the container after we return.
               We first make declarations for all the fields and then return 
               a special entry which when we apply the "env" environment
               function to it gives us returns.  That way if we never actually
               use this tuple as a single entity it won't be created.
               If we don't actually use a field the corresponding declaration
               will be removed in cleanCode. *)
            val optCont = optimise(container, NONE)
            (* Since "container" will always be an Extract entry we can have multiple
               references to it in the declarations.  Include an assertion to that
               effect just in case future changes make that no longer true. *)
            val _ =
                case optGeneral optCont of
                   Extract _ => ()
                | _ => raise InternalError "optimise - container is not Extract"
            val baseAddr = !spval
            val _ = spval := baseAddr + size
            val specialDecs =
                List.tabulate(size, fn n => mkDec(n+baseAddr, mkInd(n, optGeneral optCont)))
            val specialEntries = List.tabulate(size, fn n => mkLoad(n+baseAddr, 0))
            fun env (l:loadForm, depth, _) : optVal =
                changeLevel (simpleOptVal(Extract l)) (depth - nestingOfThisProcedure)
        in
            optVal 
                {
                  general = TupleFromContainer(optGeneral optCont, size),
                  special = Recconstr specialEntries,
                  environ = env,
                  decs    = optDecs optCont @ specialDecs,
                  recCall = ref false
                }
        end

      |  optimise (SetContainer{container, tuple, size}, _) =
            (
                (* Push the set-container down the tree and then process it. If we've
                   expanded an inline function we want to be able to find any
                   tuple we're creating. *)
                case tuple of
                    Cond _ => optimise(mkSetContainer(container, tuple, size), NONE)
                |   Newenv _ => optimise(mkSetContainer(container, tuple, size), NONE)
                |   _ =>
                    let
                        val optCont = general container
                        and optTuple = general tuple
                        (* If the "tuple" is an expanded inline function it may well
                           contain an if-expression.  If both branches were tuples
                           we will have expanded it already and the result will be
                           a TupleFromContainer. *)
                        fun pushSetContainer(Cond(ifpt, thenpt, elsept), decs) =
                            Cond(ifpt,
                                wrapEnv(List.rev(pushSetContainer(thenpt, []))),
                                wrapEnv(List.rev(pushSetContainer(elsept, [])))
                            ) :: decs

                        |   pushSetContainer(Newenv env, decs) =
                            let
                                (* Get the declarations off the block and apply
                                   pushSetContainer to the last. *)
                                fun applyToLast (_, []) = raise List.Empty
                                  | applyToLast (d, [last]) = pushSetContainer(last, d)
                                  | applyToLast (d, hd :: tl) =
                                        applyToLast(hd :: d, tl)
                            in
                                applyToLast(decs, env)
                            end

                        |   pushSetContainer(tuple as
                                TupleFromContainer(
                                    Extract{addr=innerAddr, level=0, fpRel=true, ...}, innerSize),
                                decs) =
                            if innerSize = size
                            then
                                (
                                case optCont of
                                    Extract{addr=containerAddr, level=0, fpRel=true, ...} =>
                                    let
                                        (* We can remove the inner container and replace it by
                                           a reference to the outer. *)
                                        fun replaceContainerDec [] =
                                            raise InternalError "replaceContainerDec"
                                          | replaceContainerDec ((hd as Declar{addr, ...}) :: tl) =
                                                    if addr = innerAddr
                                                    then mkDec(addr, mkLoad(containerAddr, 0)) :: tl
                                                    else hd :: replaceContainerDec tl 
                                          | replaceContainerDec(hd :: tl) =
                                                hd :: replaceContainerDec tl
                                    in
                                        (* Just replace the declaration. *)
                                        replaceContainerDec decs 
                                    end
                                | _ => SetContainer{container = optCont, tuple = tuple, size = size}
                                         :: decs
                                )
                            else SetContainer{container = optCont, tuple = tuple, size = size} :: decs

                        |   pushSetContainer(tuple, decs) =
                                SetContainer{container = optCont, tuple = tuple, size = size} :: decs
                    in
                        simpleOptVal(wrapEnv(List.rev(pushSetContainer(optTuple, []))))
                    end
            )

      |  optimise (Global g, _) = g

      |  optimise (TagTest{test, tag, maxTag}, _) =
            let
                val optTest = general test
            in
                case optTest of
                    Constnt testResult =>
                        if isShort testResult andalso toShort testResult = tag
                        then simpleOptVal CodeTrue
                        else simpleOptVal CodeFalse
                |   _ => simpleOptVal(TagTest{test=optTest, tag=tag, maxTag=maxTag})
            end

      |   optimise(IndirectVariable{base, offset}, tailCall) =
            (* If this is a constant offset turn it into a simple Indirect instr.
               TODO: If the "base" is actually a TupleVariable we may be able to
               optimise it away in the same way as with fixed-size tuples. *)
            let
                val optOffset = general offset
            in
                case optOffset of
                    Constnt offs =>
                        optimise(Indirect{base=base, offset=Word.toInt(toShort offs)}, tailCall)
                |   _ =>
                    simpleOptVal(IndirectVariable{base=general base, offset=optOffset})
            end

      |   optimise(TupleVariable(vars, length), _) =
            (* Try to turn this into a fixed size tuple. *)
            let
                val optLength = general length
            in
                case optLength of
                    Constnt _ =>
                        (* The total length is the sum of all the individual lengths so if it is
                           a constant then all the offsets and lengths must be.  We can turn this
                           into a fixed-size tuple.  We need new declarations at least for the multiple
                           entries so that they are used only once.  Doing that requires us to do the
                           optimisation at this stage rather than generating a Reccons and calling
                           "optimise" recursively so this duplicates that code. *)
                        let
                            val newTab = stretchArray(List.length vars+1, NONE)
                            fun setTab (i, v) = StretchArray.update (newTab, i, SOME v);
                            (* The record construction is treated as a block of local
                               declarations so that any expressions which might have side-effects
                               are done exactly once. *)
                            fun makeOneEntry(optSource, addr) =
                            let
                                (* Put the source into a local variable. *)
                                val newDecs = newDecl (setTab, optSource, addr, true)
                                val thisArg = valOf (StretchArray.sub(newTab, addr)) (* Get the value back. *)
                                val gen = optGeneral thisArg
                                (* If there's no "special" and the general is a constant use that for
                                   the special entry otherwise load the original value. *)
                                val specArg =
                                    case (optSpecial thisArg, gen) of
                                        (CodeNil, Constnt _) => gen
                                    |   _ => mkLoad (addr, 0)
                            in
                                { gen=gen, spec=specArg, decs=newDecs }
                            end

                            fun makeDecs([], _) = {decs = [], gen = [], spec = []}

                            |   makeDecs (VarTupleSingle{source, ...} :: t, addr) =
                                let
                                    val {gen, spec, decs} = makeOneEntry(optimise(source, NONE), addr)
                                    val {decs=restDecs, gen=genRest, spec=specRest} = makeDecs(t, addr + 1)
                                in
                                    {gen = gen :: genRest, spec = spec :: specRest, decs = decs @ restDecs }
                                end

                            |   makeDecs (VarTupleMultiple{base, length, sourceOffset, ...} :: t, addr) =
                                let
                                    val len =
                                        case general length of
                                            Constnt l => Word.toInt(toShort l)
                                        |   _ => raise InternalError "makeDecs: not constant"
                                    (* Put the base address into a local variable.  We must do this because
                                       the base code could have side-effects which must be done exactly once. *)
                                    val baseDecs = newDecl (setTab, optimise(base, NONE), addr, true)
                                    val thisArg = valOf (StretchArray.sub(newTab, addr)) (* Get the value back. *)
                                    val {decs=restDecs, gen=genRest, spec=specRest} = makeDecs(t, addr + len+ 1)

                                    (* Each entry in the result is found by indirection on the base. *)
                                    fun makeEntries n =
                                        if n = len then {decs = [], gen = [], spec = []}
                                        else
                                        let
                                            (* The offset could be a variable even if the resulting size is
                                               a constant. *)
                                            val indirectEntry =
                                                case general sourceOffset of
                                                    Constnt offset => doIndirection(thisArg, n + Word.toInt(toShort offset))
                                                |   varOffset =>
                                                    let
                                                        (* This isn't currently generated so leave this in until it is. *)
                                                        val _ = raise InternalError "Untested"
                                                        (* Add the offsets *)
                                                        val newOffset =
                                                            if n = 0 then varOffset
                                                            else mkEval(rtsFunction RuntimeCalls.POLY_SYS_plus_word,
                                                                        [varOffset, mkConst(toMachineWord n)], true)
                                                    in
                                                        simpleOptVal(
                                                            IndirectVariable{base=optGeneral thisArg, offset=newOffset})
                                                    end
                                            val thisEntry = makeOneEntry(indirectEntry, addr+n+1)   
                                            val {decs, gen, spec} = makeEntries(n+1)
                                        in
                                            {decs = #decs thisEntry @ decs, gen = #gen thisEntry :: gen,
                                             spec = #spec thisEntry :: spec}
                                        end

                                    val {gen, spec, decs} = makeEntries 0
                                in
                                    {gen = gen @ genRest, spec = spec @ specRest, decs = baseDecs @ decs @ restDecs }
                                end
          
                            val {gen, spec, decs} = makeDecs(vars, 1)
                            val newRec  = Recconstr gen
                            (* Package up the declarations so we can extract the special values. *)
                            val vec = StretchArray.vector newTab
                            fun env ({addr, ...}:loadForm, depth, _) : optVal =
                                changeLevel (valOf (Vector.sub(vec, addr))) (depth - nestingOfThisProcedure)
                        in
                            optVal 
                            {
                                (* If all the general values are constants we can create the tuple now. *)
                                general = if List.all isConstnt gen then makeConstVal newRec else newRec,
                                special = Recconstr spec,
                                environ = env,
                                decs    = decs,
                                recCall = ref false
                            }
                        end
                |   _ => (* Can't convert it to a fixed-size tuple.
                           TODO: We should treat this in the same way as a fixed record so that selection can be
                           optimised.  This isn't quite so important as for fixed-size tuples.  Fixed-size tuples
                           are used for structures and there we definitely need to be able to extract inline
                           functions.  *)
                    let
                        fun optTuple(VarTupleSingle{source, destOffset}) =
                                VarTupleSingle{source=general source, destOffset=general destOffset}
                        |   optTuple(VarTupleMultiple{base, length, destOffset, sourceOffset}) =
                                VarTupleMultiple{base=general base, length=general length,
                                                 destOffset=general destOffset, sourceOffset=general sourceOffset}
                        val optFields = map optTuple vars
                    in
                        simpleOptVal(TupleVariable(optFields, optLength))
                    end
            end

      |  optimise (Case _, _) = raise InternalError "optimise: Case"

      |  optimise (Declar _, _) = raise InternalError "optimise: Declar"

      |  optimise (MutualDecs _, _) = raise InternalError "optimise: MutualDecs"

      |  optimise (KillItems _, _) = raise InternalError "optimise: StaticLinkCall"

    (* optimise *);

(*****************************************************************************)
(*                  body of optimiseProc                                     *)
(*****************************************************************************)
  in
    optimise(pt, tailCallEntry)
  end (* optimiseProc *)



    fun codetreeOptimiser(pt, eval, maxInlineSize) =
    let
        val localAddressAllocator = ref 1
        val oldAddrTab = stretchArray (initTrans, NONE);
        val newAddrTab = stretchArray (initTrans, NONE);

        fun lookupOldAddr ({addr, level, fpRel, ...}: loadForm, depth, 0) =
        (
            case oldAddrTab sub addr of
                SOME v => changeLevel v depth
            |   NONE =>
                let
                    val msg =
                        concat["lookupOldAddr: outer level. (Addr=", Int.toString addr,
                            " Level=", Int.toString level, " Fprel=", Bool.toString fpRel, ")"]
                in
                    raise InternalError msg
                end
        )
        |   lookupOldAddr ({addr, level, fpRel, ...}, _, _) =
            let
                val msg =
                    concat["lookupOldAddr: outer level. (Addr=", Int.toString addr,
                        " Level=", Int.toString level, " Fprel=", Bool.toString fpRel, ")"]
            in
                raise InternalError msg
            end;

        fun lookupNewAddr ({addr, ...}: loadForm, depth, 0) =
        (
            case newAddrTab sub addr of
                NONE => simpleOptVal(mkGenLoad (addr, depth, true, false))
            |   SOME v => changeLevel v depth 
            )
        |  lookupNewAddr _ = raise InternalError "outer level reached in lookupNewAddr"

        fun enterNewDec (addr, tab) = update (newAddrTab, addr, SOME tab)

        fun enterDec (addr, tab) =
        (
            (* If the general part is an Extract entry we need to add an entry to
               the new address table as well as the old one.  This is sufficient
               while newDecl does not treat Indirect entries specially. *)
            case optGeneral tab of
                Extract{addr=newAddr, ...} => enterNewDec (newAddr, tab)
            |   _ => ();
            update (oldAddrTab, addr, SOME tab)
        )


        val resultCode =
          optimiseProc
            {pt=pt, lookupNewAddr=lookupNewAddr, lookupOldAddr=lookupOldAddr, enterDec=enterDec,
             enterNewDec=enterNewDec, nestingOfThisProcedure=0, spval=localAddressAllocator, earlyInline=false,
             evaluate=eval, tailCallEntry=NONE, recursiveExpansions=[], maxInlineSize=maxInlineSize }

    in
        (* Turn the arrays into vectors. *)
        StretchArray.freeze oldAddrTab;
        StretchArray.freeze newAddrTab;
        (resultCode, ! localAddressAllocator + 1)

    end

    structure Sharing = struct type codetree = codetree and optVal = optVal end

end;
