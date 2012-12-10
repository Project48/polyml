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

functor CODETREE_REMOVE_REDUNDANT(
    structure BASECODETREE: BaseCodeTreeSig
    structure CODETREE_FUNCTIONS: CodetreeFunctionsSig

    sharing BASECODETREE.Sharing = CODETREE_FUNCTIONS.Sharing
) :
    sig
        type codetree
        type loadForm = { addr : int, level: int, fpRel: bool, lastRef: bool }
        val cleanProc : (codetree * (loadForm * int * int -> codetree) * int * bool array) -> codetree
        structure Sharing: sig type codetree = codetree end
    end
=
struct
    open BASECODETREE
    open CODETREE_FUNCTIONS
    exception InternalError = Misc.InternalError

    (* Remove redundant declarations from the code.  This reduces
     the size of the data structure we retain for inline functions
     and speeds up compilation.  More importantly it removes internal
     functions which have been completely inlined.  These can mess up
     the optimisation which detects which parameters to a recursive
     function are unchanged.   It actually duplicates work that is
     done later in preCode but adding this function significantly
     reduced compilation time by reducing the amount of garbage
     created through inline expansion. DCJM 29/12/00. *)
    fun cleanProc (pt: codetree, prev: loadForm * int * int -> codetree,
                   nestingDepth, locals): codetree =
    let
        fun cleanLambda({body, isInline, name, argTypes, resultType, level=nestingDepth, localCount, ...}) =
        let
            (* Start a new level. *)
            fun lookup(ext as {addr, fpRel, ...}, 0, _) =
                (
                    (* Mark any access to local variables. *)
                    if addr >= 0 andalso fpRel
                    then Array.update(locals, addr, true)
                    else (); (* Argument or recursive reference. *)
                    Extract ext
                )

            |   lookup(ext, level, depth) = prev(ext, level-1, depth);

            val newLocals = Array.array (localCount (* Initial size. *), false);
            val bodyCode = cleanProc(body, lookup, nestingDepth, newLocals)
        in
            {body=bodyCode, isInline=isInline, name=name,
               closure=[], argTypes=argTypes, resultType=resultType, level=nestingDepth,
               closureRefs=0, makeClosure=false, localCount=localCount, argLifetimes = []}
        end

        and cleanCode (Newenv(decs, exp)) =
            let
                local
                    (* This used to clear entries in case they were reused. Now check they aren't reused. *)
                    fun checkAddr addr =
                        if Array.sub(locals, addr) then raise InternalError "checkAddr: Reused" else ()

                    fun clearEntry(Declar{addr, ...}) = checkAddr addr

                    |   clearEntry(RecDecs decs) = List.app (checkAddr o #addr) decs

                    |   clearEntry(NullBinding _) = ()
                in
                    val () = List.app clearEntry decs
                end
                
                (* First process the expression so as to mark any references it makes. *)
                val processedExp = cleanCode exp
                
                (* Process the declarations in reverse order.  A binding may be used in
                   a later declaration but apart from mutually-recursive functions no binding
                   can be used in an earlier one. *)
                fun processDecs [] = []
                 |  processDecs(Declar{value, addr, references} :: rest) =
                    let
                        val processedRest = processDecs rest
                    in
                        (* If this is used or if it has side-effects we
                           must include it otherwise we can ignore it. *)
                        if Array.sub(locals, addr) orelse not (sideEffectFree value)
                        then Declar{value=cleanCode value, addr=addr,
                                    references=references} :: processedRest
                        else processedRest
                    end

                 |  processDecs(RecDecs decs :: rest) =
                    let
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
                                (this as {addr, lambda, references}) :: rest)
                                        excluded added =
                            if not (Array.sub(locals, addr))
                            then (* Put this on the excluded list. *)
                                processMutuals rest (this::excluded) added
                            else
                            let
                                (* Process this entry - it may cause other
                                   entries to become "used". *)
                                val newEntry =
                                    {lambda=cleanLambda lambda, addr=addr, references=references}
                            in
                                newEntry :: processMutuals rest excluded true
                            end
                        val processedDecs = processMutuals decs nil false
                    in
                        if null processedDecs
                        then processedRest
                        else RecDecs processedDecs :: processedRest
                    end

                 |  processDecs(NullBinding exp :: rest) =
                    let
                        val processedRest = processDecs rest
                        val newExp = cleanCode exp
                    in
                        if sideEffectFree newExp andalso not(null processedRest)
                        then processedRest
                        else NullBinding newExp :: processedRest
                    end

                val processedDecs = processDecs decs
            in
                mkEnv(processedDecs, processedExp)
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

         |  cleanCode (Lambda lam) = Lambda(cleanLambda lam)

            (* All the other case simply map cleanCode over the tree. *)
         |  cleanCode MatchFail = MatchFail

         |  cleanCode (AltMatch(a, b)) = AltMatch(cleanCode a, cleanCode b)

         |  cleanCode (c as Constnt _) = c

         |  cleanCode (Indirect{base, offset}) =
                Indirect{base=cleanCode base, offset=offset}

         |  cleanCode (Eval{function, argList, resultType}) =
                Eval{function=cleanCode function, argList = map (fn (c, t) => (cleanCode c, t)) argList,
                     resultType=resultType}

         |  cleanCode(Cond(test, thenpt, elsept)) =
                Cond(cleanCode test, cleanCode thenpt, cleanCode elsept)

         |  cleanCode(BeginLoop{loop=body, arguments=argList, ...}) =
            let
                val processedBody = cleanCode body
                fun copyDec({addr, value, ...}, typ) =
                        ({addr=addr, value=cleanCode value, references=0}, typ)
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

         |  cleanCode (Case _) = raise InternalError "cleanCode: Case"

         |  cleanCode (Global _) = raise InternalError "cleanCode: Global"
         
         |  cleanCode (KillItems _) = raise InternalError "cleanCode: StaticLinkCall"
   in
        cleanCode pt
   end (* cleanProc *);

    structure Sharing =
    struct
        type codetree = codetree
    end
end;
